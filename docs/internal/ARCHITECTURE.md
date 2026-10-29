# Architecture — one badge, a whole plant network

> SYSRUPT Portable OT Range V2 — canonical architecture reference.
> Ports, challenge numbers and points are taken from the source
> (`Devices/vanguard/source/ui/Challenges.h`, `source/ot/*`).

## Overview

The entire range runs on **one ESP32-S3 "Vanguard" badge**. The badge boots its own Wi-Fi access point
and *is* the control network: a single IP, `10.0.1.1`, hosts every service. It is built as a
[Tactility OS](https://tactilityproject.org) **device** — Tactility provides the OS, display/input
drivers, LVGL UI and Wi-Fi; SYSRUPT adds the simulated plants, protocol servers, the LCD challenge UI,
scoring/persistence and the embedded player toolkit.

```
            ┌─────────────────────────────────────────────┐
            │              PLAYER LAPTOP                   │
            │   browser · sysrupt CLI · nmap (gentle)      │
            └───────────────────┬─────────────────────────┘
                                │ Wi-Fi: SYSRUPT-OT-V2-...
                                │ (laptop 10.0.1.x, badge 10.0.1.1)
            ┌───────────────────▼─────────────────────────┐
            │          ESP32-S3 "Vanguard" badge           │
            │        Wi-Fi AP + gateway 10.0.1.1           │
            │             (IPv4 forward + NAPT)            │
            │                                              │
            │  WEB :80  ── OtHttp (reactor HMI)            │
            │           ── DefWeb (intranet / mail /       │
            │                     investigate)             │
            │           ── HistWeb (historian console)     │
            │           ── /toolkit.zip (embedded CLI)     │
            │                                              │
            │  PROTOCOL LISTENERS                           │
            │    Modbus/TCP          502                   │
            │    IEC 60870-5-104     2404                  │
            │    TriStation-lite     1502                  │
            │    OPC-UA-lite         9005                  │
            │    Pump radio          7700                  │
            │    GOOSE (training)    6180/udp              │
            │    SNMP                161/udp               │
            │                        │                     │
            │  SIMULATED PLANTS  ◄────┘  (source/ot/sim)   │
            │    reactor · pump · substation · SIS ·       │
            │    SNMP RTU · GOOSE source                   │
            │                        │ "solved?"           │
            │  LCD UI (source/ui) ◄──┘                     │
            │    challenge registry → menu → scoring → NVS │
            └──────────────────────────────────────────────┘
```

## Services and ports

| Service | Where | Protocol | Port | Used by |
|---------|-------|----------|------|---------|
| Reactor HMI | `OtHttp` | HTTP | 80 | #11, #12 context; live process view |
| Corporate intranet / IR webmail / submit | `DefWeb` | HTTP | 80 | #07, #09, #10 |
| Process historian console | `HistWeb` | HTTP | 80 | #07 |
| Toolkit download | web | HTTP | 80 | all (get `sysrupt`) |
| Reactor control | `OtTcp` (Modbus) | Modbus/TCP | 502 | #11, #12 |
| Substation breaker | `Iec104Service` | IEC 60870-5-104 | 2404 | #01 |
| Safety controller | `SisService` | TriStation-lite | 1502 | #02 |
| OPC server | `OpcUaService` | OPC-UA-lite | 9005 | #03 |
| Pump radio gateway | `PumpService` | custom | 7700 | #04 |
| GOOSE bay | `GooseService` | GOOSE (training) | 6180/udp | #06 |
| Field RTU | `SnmpService` | SNMP | 161/udp | #08 |

`startAllServices()` brings every backend up **once** at boot (idempotent, race-safe), so the whole
range is attackable from any LCD screen — you don't have to "select" a challenge to attack it.

## Firmware structure

```
Devices/vanguard/
  source/
    ot/           network-facing range
      sim/        simulated plants (pure logic, host-testable): reactor, pump, historian,
                  substation, SIS, SNMP device, GOOSE source, SQLi simulator …
      net/        protocol servers + :80 web server (OtHttp, DefWeb, HistWeb) + ToolkitZip
      *.cpp/.h    per-protocol Service wrappers (Iec104Service, SisService, …) that own the
                  listener task and bridge a sim to the network
      ui/         OT-specific UI helpers
    ui/           LCD UI: challenge registry (Challenges.h), menu, per-challenge view,
                  scoring, NVS persistence
  test/           host-side C++ unit tests (run_all.sh)
  tools/sysrupt/  player-side Python CLI + release packaging + embedded-ZIP generator
  docs/           deep developer documentation
  bi0s,vanguard.dts   devicetree (display, SPI bus, keypad pin map)
  device.properties   Tactility device descriptor
  module.yaml         Tactility module deps + DTS reference
```

## How a challenge is "solved"

1. The player drives a protocol (e.g. writes Modbus registers, forges a GOOSE frame, submits a web form).
2. The matching **simulated plant** (`source/ot/sim`) updates its state.
3. The challenge's completion predicate (defined in `Challenges.h`) checks that state — e.g. "the
   reactor safe-band bitmask has held for N seconds," "the deleted historian record is restored and
   stable."
4. A watcher posts the newly completed challenge number; the shared **Challenge Complete** screen appears
   on whichever LCD view is in front, and the score is written to NVS.

Because detection reads the live plant state over the network, a challenge is awarded no matter which
screen is open.

## Offensive vs. defensive

- **Offensive** (7: #01, #02, #03, #04, #06, #11, #12) — *make the process do something* over an
  industrial protocol. Completion = the simulated plant reaching the target condition.
- **Defensive** (4: #07, #08, #09, #10) — investigation / recovery / submission served from the shared
  `DefService` web surface (`/mail`, `/intranet`, `/investigate`) plus the SNMP RTU. Completion = a
  verified submission or a restored/verified device state.

## Scoring and persistence

Points are difficulty-weighted and defined per challenge. The maximum is enforced at compile time:

```cpp
static_assert(SCORE_MAX == 2500, "Total attainable score must be exactly 2500 across the 11 challenges");
static_assert(CHALLENGE_COUNT + DEFENSIVE_COUNT == 11, "Exactly 11 active challenges expected");
```

Two optional hint tiers per challenge (`GET STARTED` 10 pts, `IMPORTANT LEAD` 20 pts) permanently deduct
their cost when revealed: `netScore() = earnedScore() − hintPenaltyTotal()`, floored at 0. Completions
persist in NVS and survive a reboot; only **Reset Session** clears them.

## Protocol realism (important)

The protocol servers are **bounded training re-implementations**, sized for a no-PSRAM ESP32-S3 and
scoped to teach one concept — not complete, conformant stacks:

| Surface | Reality |
|---------|---------|
| Modbus/TCP | Function-subset server over TCP 502 |
| IEC 60870-5-104 | Reduced APDU/ASDU set for the breaker scenario |
| TriStation-lite | Simplified safety-controller protocol (**not** Triconex TriStation) |
| OPC-UA-lite | Custom, non-standard service on port **9005** (not standard OPC-UA / not 4840) |
| GOOSE (training) | GOOSE-shaped exercise over **UDP 6180** (not raw L2 multicast) |
| SNMP | Small agent sufficient for the recovery workflow |
| Historian SQLi | **Bounded pattern-matching simulator**, not a real SQL engine |

Treat a protocol name as "the concept this challenge teaches," not a claim of standard conformance.

## See also

- [NETWORK_MAP.md](NETWORK_MAP.md) — ports, addresses, and how to reach each service.
- [HARDWARE.md](HARDWARE.md) — the badge board.
- Deep developer docs: [`Devices/vanguard/docs/ARCHITECTURE.md`](../../Devices/vanguard/docs/ARCHITECTURE.md).
