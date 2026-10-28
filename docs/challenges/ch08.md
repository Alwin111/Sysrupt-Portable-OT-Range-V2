# Challenge 5 — RTU Recovery

**Protocol:** SNMP (v2c) · **Port:** 161 (UDP) · **Toolkit module:** none — use standard `net-snmp` tools

---

## The Story

Field Ops flags RTU-07 as unhealthy after the incident: its SNMP agent (`10.0.1.1:161`, read community `public`) reports a `sysDescr` of `[IMAGE CORRUPT]`, because the firmware image it's currently running isn't its own last-known-good image. The device exposes a small enterprise recovery MIB for exactly this situation — you don't need to flash anything externally, you just need to drive the device's own recovery mode over SNMP. The write community needed for the actual recovery SETs isn't handed to you directly; it's published in the device's `sysContact` field.

## What You Know

- Target: `10.0.1.1:161` (SNMPv2c), read community `public`
- `sysDescr` (`1.3.6.1.2.1.1.1.0`) currently reads something like `"...[IMAGE CORRUPT - recovery required]"`
- The write community string needed for SETs is stored in `sysContact` (`1.3.6.1.2.1.1.4.0`) — read it before attempting any write
- Enterprise recovery MIB, base `1.3.6.1.4.1.42424`:

  | OID    | Name             | Access | Meaning                                                        |
  |--------|------------------|--------|------------------------------------------------------------------|
  | `.1.0` | `fwRecoveryMode` | rw     | `0` = run mode, `1` = recovery mode                               |
  | `.2.0` | `fwRunningImage` | ro     | image currently running                                           |
  | `.3.0` | `fwLastGoodImage`| ro     | the device's own known-good image                                 |
  | `.4.0` | `fwRestoreImage` | wo     | write the image to restore (always reads back `0`)                |
  | `.5.0` | `deviceHealth`   | ro     | `1` = healthy (running image == last-good image, and in run mode) |
  | `.9.0` | `recoveryFlag`   | ro     | populated once the device is verified recovered                   |

- `fwRunningImage` and `fwLastGoodImage` are plain `INTEGER` values, not strings — even though they sound like they'd hold a version string, write `fwRestoreImage` as an integer (`i`), not a string (`s`)
- `deviceHealth` needs a few seconds after leaving recovery mode to re-validate; poll rather than reading it once immediately

## Step 1: Confirm the device is actually unhealthy

<details>
<summary>Hint</summary>

Before touching anything write-related, read the plain status OIDs with the read community to see exactly what state the device is in — the corrupted `sysDescr`, the current `deviceHealth`, and the running vs. last-good image values.

</details>

<details>
<summary>Show command</summary>

```bash
snmpget -v2c -c public 10.0.1.1 1.3.6.1.2.1.1.1.0        # sysDescr   -> shows [IMAGE CORRUPT]
snmpget -v2c -c public 10.0.1.1 1.3.6.1.4.1.42424.5.0    # deviceHealth -> 0
snmpget -v2c -c public 10.0.1.1 1.3.6.1.4.1.42424.2.0    # fwRunningImage
snmpget -v2c -c public 10.0.1.1 1.3.6.1.4.1.42424.3.0    # fwLastGoodImage
```

Expect something like:

```
iso.3.6.1.2.1.1.1.0 = STRING: "Sysrupt RTU-07 firmware img=9001 [IMAGE CORRUPT - recovery required]"
iso.3.6.1.4.1.42424.5.0 = INTEGER: 0
iso.3.6.1.4.1.42424.2.0 = INTEGER: 9001
iso.3.6.1.4.1.42424.3.0 = INTEGER: 341
```

</details>

## Step 2: Get the write community from sysContact

<details>
<summary>Hint</summary>

All of the recovery SETs below require a write community, and `public` isn't it — the device publishes the actual write community string in its standard `sysContact` field. Read that with the read community first.

</details>

<details>
<summary>Show command</summary>

```bash
snmpget -v2c -c public 10.0.1.1 1.3.6.1.2.1.1.4.0        # sysContact -> write community string
```

</details>

## Step 3: Enter recovery mode

<details>
<summary>Hint</summary>

`fwRecoveryMode` is the gate for the rest of the procedure — the device won't accept a `fwRestoreImage` write while it's in normal run mode. Set it to `1` using the write community you just pulled from `sysContact`.

</details>

<details>
<summary>Show command</summary>

```bash
snmpset -v2c -c <WRITE_COMMUNITY> 10.0.1.1 1.3.6.1.4.1.42424.1.0 i 1
```

</details>

## Step 4: Restore the last-good image

<details>
<summary>Hint</summary>

`fwRunningImage` is read-only — you can't overwrite it directly. Instead you write the value you read from `fwLastGoodImage` into `fwRestoreImage`. Both image OIDs are `INTEGER`, so the value goes in as a number (`i`), not a quoted string (`s`), even though it looks like it could be a version tag.

</details>

<details>
<summary>Show command</summary>

```bash
# using the value read from fwLastGoodImage in Step 1 (e.g. 341)
snmpset -v2c -c <WRITE_COMMUNITY> 10.0.1.1 1.3.6.1.4.1.42424.4.0 i 341
```

</details>

## Step 5: Return to run mode and poll for health

<details>
<summary>Hint</summary>

Flip `fwRecoveryMode` back to `0` to let the device re-validate. `deviceHealth` only flips to `1` once it's confirmed `fwRunningImage` now matches `fwLastGoodImage` *and* the device is out of recovery mode — that check takes a couple of seconds, so poll instead of a single fixed `sleep`.

</details>

<details>
<summary>Show command</summary>

```bash
# back to run mode
snmpset -v2c -c <WRITE_COMMUNITY> 10.0.1.1 1.3.6.1.4.1.42424.1.0 i 0

# poll until healthy
until [ "$(snmpget -v2c -c public -Ovq 10.0.1.1 1.3.6.1.4.1.42424.5.0)" = "1" ]; do sleep 1; done

# confirm the running image now matches the restored one, and grab the flag
snmpget -v2c -c public 10.0.1.1 1.3.6.1.4.1.42424.2.0    # fwRunningImage -> should now read 341
snmpget -v2c -c public 10.0.1.1 1.3.6.1.4.1.42424.9.0    # recoveryFlag -> the flag
```

</details>

## What You Learned

A device can expose perfectly standard, publicly-readable SNMP status OIDs while gating the OIDs that actually change its state behind a second, non-default community string — and that string doesn't have to live in an access-control list; it can just as easily be sitting in a plain descriptive field like `sysContact`, where nobody thinks to look for a credential. It's also worth not assuming an OID's type from what it "sounds like" it should hold (a version-looking value here is a plain integer, not a string) — SNMP will tell you the real type the moment you `snmpget` it, so check before you write.
