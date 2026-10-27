# Challenge catalogue (spoiler‑free)

The range has **11 active challenges** — **7 offensive** and **4 defensive** — worth **2500 points**
total. The catalogue below is generated from the source of truth, `source/ui/Challenges.h`
(`CHALLENGES[]` + `DEFENSIVE[]`), and matches the on‑badge menu.

> **No flags, credentials or solution steps appear here or anywhere in this repository's
> documentation.** The firmware source necessarily contains the simulated challenge logic (that is what
> makes the range playable and testable); organizer walkthroughs and answer keys are kept outside the
> public repository.

The IDs are not contiguous: challenge **#05 (Dial‑In Dam) was removed** from the live catalogue during
de‑duplication (its code is retained as reference but no longer started), so the offensive IDs are
01–04, 06, 11, 12.

## Offensive (7)

| # | Challenge | Difficulty | Pts | Surface | Learning focus |
|---|---|---|---|---|---|
| 01 | Tripping Breaker | Medium | 150 | IEC 60870‑5‑104 (TCP 2404) | Control a breaker over IEC‑104; understand IOA point lists |
| 02 | Safety Instrumented Bypass | Hard | 250 | TriStation‑lite (TCP 1502) | Defeat a safety‑controller write‑lock while a checksum still validates |
| 03 | OPC Scout | Medium | 150 | OPC‑UA‑lite (TCP 9005) | Browse a server address space and reach an unlinked node |
| 04 | Rogue Radio | Hard | 200 | Custom pump TCP (7700) | Analyse a capture and craft a fresh (non‑replay) command |
| 06 | Second Blackout | Expert | 300 | GOOSE training (UDP 6180) | Observe an unauthenticated GOOSE stream and forge an accepted state change |
| 11 | Stabilise the Reactor | Expert | 300 | Modbus/TCP (502) | Map HMI values to registers; hold a multi‑condition safe state |
| 12 | Maintenance Override | Expert | 300 | Modbus/TCP (502) | Beat a write‑protect + reconciliation loop and make a value persist |

## Defensive (4)

| # | Challenge | Difficulty | Pts | Surface | Learning focus |
|---|---|---|---|---|---|
| 07 | Historian SQL Injection | Medium | 150 | Web — `/intranet`, historian console | Find and exploit an injectable query, extract a value, submit it |
| 08 | Field Device Recovery | Hard | 350 | SNMP (UDP 161) | Investigate a tampered RTU and restore its last‑good image over SNMP |
| 09 | Trojan Historian | Hard | 250 | Web — `/mail`, `/investigate` | Detect a deleted record and restore it from the retained backup |
| 10 | Device Takeover | Hard | 100 | PROFINET DCP (saved evidence) | Identify an impostor device from a capture, inventory and OUI registry |

**Totals:** offensive 1650 + defensive 850 = **2500**.

## Objectives

Each challenge ships four high‑level objectives (shown on the badge and used to orient players) plus two
optional hint tiers (`GET STARTED`, 10 pts; `IMPORTANT LEAD`, 20 pts) that permanently deduct their cost
when revealed. The objectives describe *what to investigate*, not how to solve it:

- **01 Tripping Breaker** — find the IEC‑104 service, read the engineer's email, learn the point list,
  trip the main breaker and confirm it opened.
- **02 Safety Instrumented Bypass** — confirm a direct write is blocked, open the short‑lived program
  lock, read the rolling key, disable the trip and hold it.
- **03 OPC Scout** — read the monitoring email, browse from the root, use Diagnostics to find an orphan
  node id, read the orphaned node.
- **04 Rogue Radio** — open the saved pump capture, separate real frames from decoys, infer the
  frame/checksum rule, craft a fresh command and hold it.
- **06 Second Blackout** — observe the normal GOOSE stream, learn its identifiers, forge the next state
  number with a breaker‑open flag, send it so the relay trips.
- **11 Stabilise the Reactor** — follow `robots.txt` to the control page, read the notice, map HMI
  values to Modbus registers, hold the all‑safe condition.
- **12 Maintenance Override** — read the engineering notice, observe a locked write rejected, find the
  unlock window, write within it and make the value persist.
- **07 Historian SQL Injection** — find the historian console (off the main nav), test the
  Filter‑by‑Source field, enumerate the schema by UNION, read the flags table, submit the value at
  `/intranet`.
- **08 Field Device Recovery** — query the RTU over SNMP, find it unhealthy (running ≠ last‑good image),
  enter recovery mode, restore the last‑good image, verify it reads healthy.
- **09 Trojan Historian** — spot the trend gap, read the backup for the missing sample, restore it via
  the submit form, keep it stable.
- **10 Device Takeover** — open the saved DCP capture, find a name claimed by two MACs, check each
  against inventory + OUI registry, submit the impostor MAC.

## Scoring invariants (enforced in source)

```cpp
static_assert(SCORE_MAX == 2500, "Total attainable score must be exactly 2500 across the 11 challenges");
static_assert(CHALLENGE_COUNT + DEFENSIVE_COUNT == 11, "Exactly 11 active challenges expected");
```

A host test (`ot_catalogue_tests`) additionally checks the split is **7 offensive / 4 defensive**.
