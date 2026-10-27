<h1 align="center">SYSRUPT Portable OT Range V2</h1>

<p align="center">
  <strong>A hands-on OT/ICS cybersecurity training range that fits on a single badge.</strong>
</p>

<p align="center">
  <a href="#build-the-firmware"><img alt="ESP-IDF v6.1" src="https://img.shields.io/badge/ESP--IDF-v6.1-informational"></a>
  <img alt="Target ESP32-S3" src="https://img.shields.io/badge/target-ESP32--S3-blue">
  <img alt="Challenges 11" src="https://img.shields.io/badge/challenges-11-success">
  <img alt="Max score 2500" src="https://img.shields.io/badge/max%20score-2500-success">
  <img alt="Tests 881 C++ / 132 py" src="https://img.shields.io/badge/tests-881%20C%2B%2B%20%2F%20132%20py-brightgreen">
  <img alt="License GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-informational">
</p>

<p align="center">
  <img src="Devices/vanguard/artifacts/ui/challenge_complete.png" alt="On-badge LCD — challenge-complete screen" width="300"/>
  &nbsp;&nbsp;
  <img src="Devices/vanguard/artifacts/ui/reset_session.png" alt="On-badge LCD — reset-session screen" width="300"/>
  <br/>
  <em>The 320×240 badge LCD (UI layout references in <code>Devices/vanguard/artifacts/ui/</code>).</em>
</p>

## Overview

Join a Wi-Fi network. Hack a power plant. Learn ICS/SCADA security.

The **SYSRUPT Portable OT Range V2** is a complete industrial-cybersecurity training lab that runs
entirely on **one ESP32-S3 "Vanguard" badge**. The badge boots its own Wi-Fi access point and *becomes*
the control network of a small simulated nuclear/industrial site: web HMI, process historian, and a
handful of real industrial protocols, with **11 progressive CTF challenges** spanning offensive
exploitation and defensive investigation.

No cloud. No internet. No external infrastructure. Flash the badge, join its Wi-Fi, start hacking. Your
score and progress show on the badge's **320×240 LCD** and survive a reboot.

> **This repo contains no flags, solutions or walkthroughs.** The firmware necessarily includes the
> simulated challenge logic (that is what makes the range playable), but answer keys and solve guides are
> deliberately kept out. The challenge guides in [`docs/challenges/`](docs/challenges/) are **spoiler-free
> hints only**. See [Security & authorization](#security--authorization).

> **This is a fork of [Tactility OS](https://tactilityproject.org).** The badge firmware is a Tactility
> *device*, so all SYSRUPT code lives under **[`Devices/vanguard/`](Devices/vanguard/)**. The upstream
> Tactility overview is preserved at [`TACTILITY_UPSTREAM.md`](TACTILITY_UPSTREAM.md).

## Architecture

Unlike a rack of servers, the whole range is **one badge**. It runs a Wi-Fi access point at
`10.0.1.1`, and that single address hosts every service. The scenario is framed as a layered plant
network (an IEC 62443 / Purdue-style model), and the narrative "deeper zone" addresses are reachable
through the badge's NAT.

| Layer | What's inside | How you reach it |
|-------|---------------|------------------|
| **Web / enterprise** | Operator HMI, corporate intranet, IR webmail, process historian, toolkit download | HTTP on `10.0.1.1:80` |
| **Supervisory** | The reactor HMI and live process mimic | HTTP + Modbus |
| **Process** | Reactor, pump bus, substation breaker, safety controller | Modbus, IEC-104, TriStation-lite, pump radio |
| **Safety / field** | Safety Instrumented System, field RTU, GOOSE bay | TriStation-lite, SNMP, GOOSE |

### Live process visualisation

The operator HMI at `http://10.0.1.1/hmi` shows the plant as a connected animated process mimic with
live gauges (core temperature, pressure, neutron flux, safe band). Attacks have visible impact: drive
the reactor out of its safe band and the status pill changes; trip the bay breaker and the protection
panel flips state.

## Protocols

Each service is a **bounded training re-implementation**, sized for a no-PSRAM ESP32-S3 and scoped to
teach one concept — not a complete, conformant industry stack. Treat a protocol name as "the concept
this challenge teaches."

| Protocol / surface | Port | Real-world use | In this range |
|--------------------|------|----------------|---------------|
| **HTTP** | TCP 80 | Corporate & SCADA web systems | HMI, intranet, webmail, historian, `/toolkit.zip` |
| **Modbus/TCP** | TCP 502 | PLCs, RTUs, dosing systems | Reactor control (#11, #12) |
| **IEC 60870-5-104** | TCP 2404 | European power-grid SCADA | Breaker trip (#01) |
| **TriStation-lite** | TCP 1502 | Safety controllers (Triconex-style) | Safety bypass (#02) |
| **OPC-UA-lite** | TCP 9005 | Industrial data exchange | OPC Scout (#03) |
| **Pump radio** | TCP 7700 | Legacy radio gateways | Rogue Radio (#04) |
| **GOOSE (training)** | UDP 6180 | Substation protection (IEC 61850) | Second Blackout (#06) |
| **SNMP** | UDP 161 | Device management | Field device recovery (#08) |
| **Historian** | TCP 9000 | Process data archival | Evidence backend for #07/#09 |

> Fidelity notes: the GOOSE exercise runs over **UDP** (not raw Layer-2 multicast), OPC-UA-lite is a
> custom service on a non-standard port (not real OPC-UA on 4840), and the historian SQL-injection
> challenge uses a **bounded pattern-matching simulator**, not a real SQL engine. See
> [`docs/internal/ARCHITECTURE.md`](docs/internal/ARCHITECTURE.md).

## Challenges

Eleven progressive challenges — **7 offensive**, **4 defensive** — worth **2500 points** total. The
badge detects completion over the network and updates the scoreboard automatically; there is no manual
flag submission (except the two defensive forms that are themselves the challenge).

IDs are not contiguous — challenge **#05 (Dial-In Dam)** was retired during de-duplication.

| # | Challenge | Type | Difficulty | Points | What you'll do |
|---|-----------|------|-----------|--------|----------------|
| 01 | Tripping Breaker | Offensive | Medium | 150 | Control a substation breaker over IEC-104 |
| 02 | Safety Instrumented Bypass | Offensive | Hard | 250 | Defeat a safety-controller write-lock while the checksum still validates |
| 03 | OPC Scout | Offensive | Medium | 150 | Browse an address space and reach an unlinked node |
| 04 | Rogue Radio | Offensive | Hard | 200 | Analyse a capture and craft a fresh (non-replay) pump command |
| 06 | Second Blackout | Offensive | Expert | 300 | Observe a GOOSE stream and forge an accepted state change |
| 11 | Stabilise the Reactor | Offensive | Expert | 300 | Map HMI values to Modbus registers and hold a safe state |
| 12 | Maintenance Override | Offensive | Expert | 300 | Beat a write-protect + reconciliation loop so a value persists |
| 07 | Historian SQL Injection | Defensive | Medium | 150 | Find the hidden historian console, inject, extract, submit |
| 08 | Field Device Recovery | Defensive | Hard | 350 | Investigate a tampered RTU and restore it over SNMP |
| 09 | Trojan Historian | Defensive | Hard | 250 | Detect a deleted record and restore it from backup |
| 10 | Device Takeover | Defensive | Hard | 100 | Identify an impostor device from a saved capture |

Spoiler-free hint guides for every challenge: **[Challenge Guide](#challenge-guide)**.

## Under the Hood

Everything runs on the one badge:

- **Wi-Fi access point** (`SYSRUPT-OT-V2-…`, gateway `10.0.1.1`) with IPv4 forwarding + NAPT.
- **~9 concurrent network services** — a `:80` web server (HMI, intranet, webmail, historian) plus the
  industrial-protocol listeners.
- **Simulated plants** (`source/ot/sim`) — a reactor, pump bus, substation, safety system, historian,
  SNMP RTU and GOOSE source, all pure logic so they can be unit-tested on a laptop.
- **LCD challenge UI** (`source/ui`) — a 320×240 browser with per-challenge story, objectives, two
  optional hint tiers, live status and a shared "challenge complete" screen.
- **On-device scoring** — completions are stored in NVS and survive a reboot; only a deliberate *Reset
  Session* clears them.
- **Embedded player toolkit** — the `sysrupt` CLI is baked into the firmware and served at
  `/toolkit.zip`, provably byte-identical to the release archive.

`Devices/vanguard/source/ui/Challenges.h` is the **single source of truth** for the catalogue, points
and completion predicates.

## Getting Started

### What you need

- An **ESP32-S3 "Vanguard" badge** (or a compatible ESP32-S3 dev board + the firmware).
- A laptop with Wi-Fi (Kali or any Linux works best; the toolkit needs only **Python 3.9+**).
- A USB-C cable to flash the badge.

### Flash the badge

**Easiest:** flash the pre-built `build/Tactility.bin` with the included helper:

```bash
./flash_badge.sh -p /dev/ttyACM0 -y      # preserves saved progress (NVS); add --erase for a clean wipe
```

**From source:** see [INSTALL.md](INSTALL.md) — one Docker command builds the firmware with ESP-IDF v6.1.

### Connect and start hacking

```bash
# 1. Join the badge's open Wi-Fi:  SYSRUPT-OT-V2-...
# 2. Your laptop gets 10.0.1.x automatically; the badge is 10.0.1.1
# 3. Open the operator HMI in a browser:
firefox http://10.0.1.1/

# 4. Download the protocol toolkit from the badge:
curl -O http://10.0.1.1/toolkit.zip
unzip toolkit.zip && cd sysrupt-toolkit-1.0.0
./bin/sysrupt status        # read-only probe of the challenge ports
```

> **Be gentle.** The ESP32-S3 has no PSRAM and a tiny network stack — use **one connection at a time,
> low rate**. An aggressive `nmap -T4` sweep makes ports read *filtered* (dropped), not "closed".

### The `sysrupt` toolkit

A single dependency-free Python CLI, served by the badge, that speaks the challenge protocols with a
four-level verification ladder (request → ack → observed state change → challenge completion):

```bash
./bin/sysrupt help            # grouped command reference
./bin/sysrupt help modbus     # one protocol's commands
./bin/sysrupt config set target 10.0.1.1
```

| Group | Covers |
|-------|--------|
| `modbus` | Modbus/TCP read/write (incl. timing-sensitive variants) — #11, #12 |
| `iec104` | IEC-104 interrogation & control — #01 |
| `tristation` | TriStation-lite read/dump/watch — #02 |
| `goose` | GOOSE sniff / decode / transmit / sustain — #06 |

Full reference: [`Devices/vanguard/tools/sysrupt/COMMANDS.md`](Devices/vanguard/tools/sysrupt/COMMANDS.md) ·
guide: [`Devices/vanguard/docs/TOOLKIT.md`](Devices/vanguard/docs/TOOLKIT.md).

## Challenge Guide

Progressive hints for each challenge (**no spoilers, no flags**):

| Challenge | Guide | | Challenge | Guide |
|-----------|-------|-|-----------|-------|
| 01 · Tripping Breaker | [ch01.md](docs/challenges/ch01.md) | | 09 · Trojan Historian | [ch09.md](docs/challenges/ch09.md) |
| 02 · Safety Instrumented Bypass | [ch02.md](docs/challenges/ch02.md) | | 10 · Device Takeover | [ch10.md](docs/challenges/ch10.md) |
| 03 · OPC Scout | [ch03.md](docs/challenges/ch03.md) | | 11 · Stabilise the Reactor | [ch11.md](docs/challenges/ch11.md) |
| 04 · Rogue Radio | [ch04.md](docs/challenges/ch04.md) | | 12 · Maintenance Override | [ch12.md](docs/challenges/ch12.md) |
| 06 · Second Blackout | [ch06.md](docs/challenges/ch06.md) | | | |
| 07 · Historian SQL Injection | [ch07.md](docs/challenges/ch07.md) | | | |
| 08 · Field Device Recovery | [ch08.md](docs/challenges/ch08.md) | | | |

The documentation site (same content) is in [`docs/`](docs/) — start at [`docs/index.md`](docs/index.md).

## Hardware

The "Vanguard" badge is a custom ESP32-S3 board. The authoritative description is the devicetree
[`Devices/vanguard/bi0s,vanguard.dts`](Devices/vanguard/bi0s,vanguard.dts) and
[`device.properties`](Devices/vanguard/device.properties); full detail in
[`docs/internal/HARDWARE.md`](docs/internal/HARDWARE.md).

| | |
|---|---|
| **SoC** | ESP32-S3-WROOM-1, 16 MB flash (DIO), **no PSRAM** |
| **Display** | ST7789, 320×240 landscape, FSPI @ 40 MHz |
| **Input** | 5-way joystick + S1–S5 buttons (active-low) |
| **Storage** | Internal only (no SD); progress persisted in NVS |
| **Programming** | USB-C serial (e.g. `/dev/ttyACM0`, 115200) |
| **Networking** | Wi-Fi AP `SYSRUPT-OT-V2-NNN`, gateway `10.0.1.1`, IPv4 forwarding + NAPT |

> This repository contains **no schematic, PCB or CAD files**. The LoRa radio, LEDs, buzzer and battery
> sensing present on the board are **not** driven by the firmware.

## Real-World Relevance

The challenges mirror techniques from real ICS incidents:

| Incident | Year | Technique echoed here |
|----------|------|-----------------------|
| **Stuxnet** | 2010 | PLC manipulation + safety-system bypass (#02, #11, #12) |
| **Ukraine power grid** | 2015 | IEC-104 breaker-trip commands (#01) |
| **TRITON / TRISIS** | 2017 | Safety Instrumented System compromise (#02) |
| **Industroyer** | 2016 | GOOSE / substation protocol abuse (#06) |

## Documentation

| Topic | Document |
|-------|----------|
| Install / build / flash | [INSTALL.md](INSTALL.md) |
| Docs site landing | [docs/index.md](docs/index.md) |
| Architecture | [docs/internal/ARCHITECTURE.md](docs/internal/ARCHITECTURE.md) |
| Network map & ports | [docs/internal/NETWORK_MAP.md](docs/internal/NETWORK_MAP.md) |
| Hardware | [docs/internal/HARDWARE.md](docs/internal/HARDWARE.md) |
| Operator setup | [docs/internal/SETUP.md](docs/internal/SETUP.md) |
| Challenge guides (spoiler-free) | [docs/challenges/](docs/challenges/) |
| Deep developer docs | [Devices/vanguard/docs/](Devices/vanguard/docs/) |
| Contributing | [CONTRIBUTING.md](CONTRIBUTING.md) · [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) |
| Security policy | [SECURITY.md](SECURITY.md) |

## Security & authorization

This is a **training / CTF simulator**. Its "vulnerabilities," plaintext credentials and insecure
defaults are **intentional challenge fixtures**, confined to the simulated plants on the badge. Use the
offensive tooling here **only** against the authorized badge/lab or systems you have explicit permission
to test — **never** against real industrial equipment or production OT networks.

Challenge walkthroughs, answer keys and solve guides are deliberately **not** in this repository. To
report a *real* vulnerability (as opposed to an intended CTF weakness), see [`SECURITY.md`](SECURITY.md).

## Development status & limitations

- ✅ Firmware builds (ESP-IDF v6.1); C++ host tests (20 suites / 881 checks) and Python tests (132) pass;
  the embedded toolkit ZIP matches the release archive byte-for-byte.
- 🟡 Protocol servers are training simulations, not full standard stacks; GOOSE runs over UDP; the
  historian SQLi uses a pattern-matching simulator.
- 🔌 On-device LCD rendering, live-network behaviour, flashing and NVS persistence are exercised during
  development but not covered by an automated hardware check.

This project is **not** "production-ready" or "secure"; it is a deliberately vulnerable training tool.
Full breakdown: [`Devices/vanguard/docs/DEVELOPMENT_STATUS.md`](Devices/vanguard/docs/DEVELOPMENT_STATUS.md).

## License & attribution

This project combines code under **two licenses**, and the effective license of the combined/distributed
work is **GPL-3.0**:

- **Tactility base** (OS, drivers, platform) — GNU **GPL-3.0** ([`LICENSE.md`](LICENSE.md)).
- **SYSRUPT range code** (`Devices/vanguard/`) — **Apache-2.0** (per the SPDX headers and
  [`Devices/vanguard/LICENSE-Apache-2.0.md`](Devices/vanguard/LICENSE-Apache-2.0.md)).
- **Third-party components** (the `Libraries/` submodules, fonts, assets) retain their own licenses — see
  [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md) and [`SUBMODULES.md`](SUBMODULES.md).

Apache-2.0 is one-way compatible with GPL-3.0, so SYSRUPT code may be distributed as part of the GPL-3.0
whole; the aggregate is then governed by GPL-3.0. This does **not** relicense upstream Tactility or
third-party components.

## Acknowledgements

Built on [Tactility OS](https://tactilityproject.org). Portions of the SYSRUPT codebase were co-authored
with Anthropic's Claude.
