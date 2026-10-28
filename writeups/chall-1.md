# Challenge 1 — Tripping Breaker

**Protocol:** IEC 60870-5-104 · **Port:** 2404/tcp · **Toolkit module:** `iec104`

---

## The Story

Something in the substation logic is holding a breaker in a state it shouldn't be in. Your
job is to talk to the RTU over IEC-104, work out which monitored point actually represents the
breaker, and issue the correct command to change it — without assuming anything the device
hasn't told you.

## What You Know

- Target is reachable on `10.0.1.1:2404`.
- The toolkit's `iec104 interrogate` performs a general interrogation (`C_IC`, QOI=20) and lists
  every monitored point the RTU currently exposes.
- The toolkit will **not** tell you which IOA is the breaker — its own output says so explicitly:
  *"point meanings are not inferred from IOA numbers — consult the evidence."*

---

## Step 1: Confirm connectivity and set your target

<details>
<summary>Hint</summary>

Before touching the protocol, confirm the badge even has this port open.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt target set 10.0.1.1
./bin/sysrupt status
```
</details>

## Step 2: Enumerate every point the RTU exposes

<details>
<summary>Hint</summary>

A general interrogation returns a full snapshot. Capture it — you'll want to compare it against
later reads.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt iec104 interrogate
```
</details>

## Step 3: Separate signal from noise

<details>
<summary>Hint</summary>

Not every point is stable. Read the same IOA several times in a row and watch what happens —
some values are fixed, some flip on their own. A point that bounces unpredictably on every read
is not a status gate you need to wait on; it's telling you something else.
</details>

<details>
<summary>Another hint</summary>

Look for corroborating evidence outside the protocol itself — an HMI page, a historian trend, or
an engineering note on the badge's intranet — before deciding which IOA is the breaker control
point. Don't guess from the number alone.
</details>

<details>
<summary>Show command</summary>

```bash
for i in 1 2 3 4 5 6; do
  ./bin/sysrupt iec104 read <CANDIDATE_IOA>
  sleep 0.3
done
```

Repeat for each candidate IOA from your Step 2 dump until you can point to actual evidence for
which one is the breaker state.
</details>

## Step 4: Issue the command

<details>
<summary>Hint</summary>

Commands in IEC-104 need an originator address. If your first attempt is rejected, the response
itself is evidence — read what it says rather than retrying blindly.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt iec104 command <BREAKER_IOA> <VALUE> --originator 7
./bin/sysrupt iec104 read <BREAKER_IOA>
```
</details>

## What You Learned

- How to run a general interrogation and read individual IOAs with `sysrupt iec104`.
- That IOA numbering is not self-describing — correlating protocol data with other evidence on
  the range is part of the challenge, not a shortcut around it.
- How to tell a noisy/irrelevant point apart from a real state point by sampling it over time.
