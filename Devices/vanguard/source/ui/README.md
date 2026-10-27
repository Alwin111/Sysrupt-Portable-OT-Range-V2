# `source/ui/` — the LCD challenge interface

The on-badge 320×240 UI: the challenge browser, per-challenge views, scoring, hints and NVS persistence.

## The single source of truth

[`Challenges.h`](Challenges.h) holds two `constexpr` arrays — `CHALLENGES[]` (7 offensive) and
`DEFENSIVE[]` (4 defensive). Each entry carries the challenge's number, name, brief, backend kind,
category, difficulty, points, story, objectives, protocol and access string. **Every** screen (the
category select, the list, the per-challenge overview) reads these arrays — there is no second catalogue
to keep in sync.

```cpp
static_assert(SCORE_MAX == 2500, "Total attainable score must be exactly 2500 across the 11 challenges");
static_assert(CHALLENGE_COUNT + DEFENSIVE_COUNT == 11, "Exactly 11 active challenges expected");
```

## Scoring and hints

- Points are difficulty-weighted; `SCORE_MAX` is summed at compile time and guarded by `static_assert`.
- Two optional hint tiers per challenge (`GET STARTED` 10 pts, `IMPORTANT LEAD` 20 pts) permanently
  deduct their cost when revealed: `netScore() = earnedScore() − hintPenaltyTotal()`, floored at 0.

## Completion and persistence

A watcher derives "solved" from the live backend state (see [`../ot/`](../ot/)) and posts newly
completed challenge numbers; the shared **Challenge Complete** screen then appears over whatever view is
in front. Completions are written to NVS (`/data`), idempotently, and survive a reboot — only **Reset
Session** clears them.

See the project [architecture doc](../../docs/ARCHITECTURE.md) for dispatch and persistence detail.
