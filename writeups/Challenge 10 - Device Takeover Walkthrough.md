# Challenge 10 — Device Takeover

**Protocol:** PROFINET DCP (Discovery and Configuration Protocol) · **Layer:** Ethernet multicast, no IP/port · **Toolkit module:** none — read from the capture directly

---

## The Story

IT Security captured overnight PROFINET DCP (and surrounding ARP/LLDP) traffic on the cell network. One station name shows up claimed by two different MAC addresses — a classic device-takeover pattern, where a rogue NIC impersonates a real controller's identity on the network. You need to find the impostor's MAC and confirm it isn't a device that actually belongs on the network.

## What You Know

- Capture: `dcp_capture.txt`, filtered on `pn_dcp || arp || lldp`
- One `NameOfStation` is answered by two different source MACs across the capture
- The impostor should be confirmable against the asset inventory and the OUI (vendor prefix) registry — a real plant device's MAC should match a known asset and a known vendor OUI; a spoofed one won't

---

## Step 1: Build the legitimate name → MAC baseline

<details>
<summary>Reveal hint</summary>

Start with the DCP **Identify** exchange, not the Hello messages — a broadcast Identify Request gets an Identify Response from every real station replying with its own name and IP, so that's your trustworthy baseline before anything suspicious happens.

</details>

<details>
<summary>Reveal command / solution</summary>

```bash
grep -A2 "Identify (5) / Response" dcp_capture.txt
```

**Output:**

```text
Frame 3: Src 0a:11:22:00:00:21 -> NameOfStation "plc-01", IP 10.0.1.21
Frame 4: Src 0a:11:22:00:00:22 -> NameOfStation "rtu-02", IP 10.0.1.22
Frame 5: Src 0a:55:66:00:00:24 -> NameOfStation "hist-04", IP 10.0.1.24
```

**Baseline so far:**

| Name     | MAC                 | IP         |
|:---------|:--------------------|:-----------|
| plc-01   | `0a:11:22:00:00:21` | 10.0.1.21  |
| rtu-02   | `0a:11:22:00:00:22` | 10.0.1.22  |
| hist-04  | `0a:55:66:00:00:24` | 10.0.1.24  |

</details>

---

## Step 2: Watch for a second claimant on the same identity

<details>
<summary>Reveal hint</summary>

Right after the Identify round, watch for an ARP announcing a known device's IP from a MAC that isn't in your baseline, followed by that same new MAC sending its own DCP **Hello** claiming the same station name. A gratuitous ARP right before a DCP Hello is the signature of a device trying to take over an identity already on the network.

</details>

<details>
<summary>Reveal command / solution</summary>

```bash
grep -B1 -A3 "gratuitous\|Hello (6)" dcp_capture.txt
```

**Output:**

```text
Frame 8: Gratuitous ARP — 10.0.1.21 is at 0a:77:88:00:00:aa
Frame 9: Hello Request, Src 0a:77:88:00:00:aa -> NameOfStation "plc-01", IP 10.0.1.21
Frame 10: Hello Request, Src 0a:11:22:00:00:21 -> NameOfStation "plc-01", IP 10.0.1.21   (the real plc-01, still alive)
```

`0a:77:88:00:00:aa` is announcing itself as `plc-01` / `10.0.1.21` — the exact identity already owned by `0a:11:22:00:00:21` in your Step 1 baseline, and frame 10 shows the real `plc-01` still sending its own Hello afterward. Two devices, one claimed identity.

</details>

---

## Step 3: Confirm it against the inventory and OUI registry

<details>
<summary>Reveal hint</summary>

Two checks confirm which MAC is the impostor rather than, say, a legitimate NIC replacement: does the MAC appear in the asset inventory at all, and does its OUI (the first three octets) match the vendor prefix the other legitimate devices on this cell share?

</details>

<details>
<summary>Reveal command / solution</summary>

```text
# Asset inventory lookup (in-app: Inventory tab)
0a:11:22:00:00:21  -> listed, "plc-01"
0a:77:88:00:00:aa  -> NOT in inventory

# OUI comparison
0a:11:22:xx:xx:xx  -> matches rtu-02 and plc-01's own prefix (known vendor block for this cell)
0a:77:88:xx:xx:xx  -> different vendor block entirely, not used by any registered asset
```

Unregistered in the inventory *and* a mismatched OUI — `0a:77:88:00:00:aa` is the impostor.

</details>

---

## Step 4: Submit the finding

<details>
<summary>Reveal hint</summary>

Challenge #10 on the Record Findings page wants the impostor station's MAC exactly as captured.

</details>

<details>
<summary>Reveal command / solution</summary>

```text
mac: 0a:77:88:00:00:aa
```

</details>

---

## What You Learned

PROFINET DCP has no authentication on Identify or Hello — any device on the segment can announce itself as an existing station's name and IP, and neighboring devices have no built-in way to tell the real one from the impostor. The tell is behavioral and contextual, not cryptographic: a gratuitous ARP immediately before a conflicting Hello, a MAC absent from the asset inventory, and an OUI that doesn't match the vendor block the rest of the cell uses. None of those checks are things the protocol itself enforces — they're the operator's job, which is exactly why this kind of takeover goes unnoticed without someone watching the traffic.