# `source/ot/sim/` — simulated plants

The "physics" of the range. Each file models one plant or device as **pure logic** — no sockets, no
hardware, no ESP-IDF — so it can be unit-tested on a laptop and reused by the matching protocol service.

## What's modelled

| Plant / device | Drives challenge(s) |
|----------------|---------------------|
| Reactor | #11 Stabilise the Reactor, #12 Maintenance Override (Modbus) |
| Substation / breaker | #01 Tripping Breaker (IEC-104) |
| Safety controller (SIS) | #02 Safety Instrumented Bypass (TriStation-lite) |
| Pump bus | #04 Rogue Radio (pump radio) |
| GOOSE source | #06 Second Blackout (GOOSE/UDP) |
| SNMP RTU | #08 Field Device Recovery (SNMP) |
| Historian + SQLi simulator | #07 Historian SQL Injection, #09 Trojan Historian |

## Design rules

- **No I/O in the sim.** A `sim` exposes state and transitions; the `net`/service layer does the sockets.
  This keeps the sims deterministic and host-testable.
- **State drives completion.** A challenge is "solved" when the sim reaches a target condition
  (e.g. a safe-band bitmask held for N seconds, a restored record). The predicate lives in
  [`../../ui/Challenges.h`](../../ui/Challenges.h).
- The SQL-injection surface is a **bounded pattern-matching simulator**, not a real SQL engine.

## Testing

```bash
cd Devices/vanguard/test && ./run_all.sh   # 20 C++ suites / 881 checks
```
