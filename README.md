# SYSRUPT OT Range V2

A **portable OT/ICS Capture‑the‑Flag range on a badge.** A single ESP32‑S3 "Vanguard" badge boots into
its own Wi‑Fi access point and *becomes* the control network of a small industrial site. You join the
badge's Wi‑Fi, speak each plant's industrial protocol, and capture a flag when you make the process do
what the challenge asks. **11 challenges** (offensive + defensive), no external infrastructure, no
internet, no cloud.

> **This repository does not contain challenge solutions or flags.** The firmware source necessarily
> contains the simulated challenge logic; challenge *walkthroughs, answer keys, and solve guides are
> kept out of this public repository.* See [Security & authorization](#security--authorization).

> Built as a **device on top of [Tactility OS](https://tactilityproject.org)** — this repo is a
> Tactility fork because the badge firmware is a Tactility *device*. The upstream Tactility README is
> preserved at [`TACTILITY_UPSTREAM.md`](TACTILITY_UPSTREAM.md). **All SYSRUPT code lives under
> [`Devices/vanguard/`](Devices/vanguard/).**

---

## Capabilities

- A self‑contained ICS network on one badge: Modbus/TCP, IEC‑104, a TriStation‑lite safety protocol,
  an OPC‑UA‑lite service, a GOOSE training stream, SNMP, and a web historian — all simulated.
- **11 challenges** across offensive exploitation and defensive investigation/recovery.
- An on‑badge **320×240 LCD UI** (challenge browser, live telemetry, hints, scoring) and a web UI
  served from the badge at `http://10.0.1.1`.
- Local **scoring + progress persistence** in NVS (survives reboot), max score **2500**.
- A player‑side, dependency‑free **`sysrupt` CLI toolkit** (downloadable from the badge).

## Hardware & software requirements

- **Hardware:** an ESP32‑S3 "Vanguard" badge (16 MB flash, 320×240 ST7789 LCD, no PSRAM), USB‑C.
- **Build:** [ESP‑IDF **v6.1**](https://docs.espressif.com/projects/esp-idf/) (the pinned toolchain).
  The simplest path is the official Docker image `espressif/idf:v6.1`.
- **Host tests:** a C++17 compiler (`g++`) — no ESP hardware needed.
- **Toolkit:** Python **3.9+** (standard library only — no third‑party runtime dependencies).

## Architecture (high level)

```
Devices/vanguard/
  source/
    ot/sim/    simulated plants (reactor, pump, historian, substation, SIS, SNMP device, …)
    ot/net/    protocol servers + the :80 web server (OtHttp, DefWeb, HistWeb, /toolkit.zip)
    ui/        LCD UI (challenge registry + views + scoring/NVS persistence)
  test/        host‑side C++ unit tests (run_all.sh)
  tools/sysrupt/   the player‑side Python CLI + its release packaging
```
The **challenge registry** (`source/ui/Challenges.h`) is the single source of truth for the catalogue,
categories, points and completion predicates; scoring is recomputed from an NVS‑persisted completion
bitmask. The web server embeds the toolkit release archive and serves it at `/toolkit.zip`. For a fuller
description see [`docs/`](docs/) *(architecture notes)*.

## Get the source

The third‑party libraries under `Libraries/` are **git submodules**, so clone recursively (or init
them after cloning). See [`SUBMODULES.md`](SUBMODULES.md).

```bash
git clone --recursive <this-repo-url>
# or, after a plain clone:
git submodule update --init --recursive
```

## Build the firmware

```bash
# from the repository root, using the pinned ESP‑IDF toolchain via Docker:
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
```
The build fetches ESP‑IDF managed components from `dependencies.lock` on first run. Output is
`build/Tactility.bin` (~3.3 MB; the app partition is 6 MB).

## Flash a badge

```bash
./flash_badge.sh -p /dev/ttyACM0 -y     # preserves NVS (saved progress); --erase for a clean wipe
```

## Run the tests

```bash
# C++ host unit tests (no hardware):
cd Devices/vanguard/test && ./run_all.sh

# Python toolkit tests:
cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q
```

## The `sysrupt` player toolkit

A single dependency‑free CLI for the protocol work. It is served by the badge and can be downloaded
after authenticating to the engineering console:

```
http://10.0.1.1/toolkit.zip
```
Then:
```bash
unzip sysrupt-toolkit-1.0.0.zip && cd sysrupt-toolkit-1.0.0
./bin/sysrupt help          # grouped command reference (also in COMMANDS.md)
./bin/sysrupt status        # probe the badge's challenge ports
```
It speaks Modbus/TCP, IEC‑104, TriStation‑lite and GOOSE, with a four‑level verification ladder
(request → ack → observed state change → challenge completion) and an opt‑in timing/sequence engine.
See [`Devices/vanguard/tools/sysrupt/README.md`](Devices/vanguard/tools/sysrupt/README.md).

### Rebuilding the toolkit release + embedded asset
```bash
cd Devices/vanguard/tools/sysrupt
python3 make_release.py                                   # -> release/sysrupt-toolkit-1.0.0.zip
python3 gen_embedded_zip.py release/sysrupt-toolkit-1.0.0.zip   # -> source/ot/net/ToolkitZip.{h,cpp}
python3 gen_commands.py                                   # -> COMMANDS.md (from the command registry)
```
A host test (`ot_toolkitzip_tests`) asserts the embedded bytes equal the release ZIP byte‑for‑byte.

## Challenge catalogue (11)

| # | Challenge | Category | Difficulty | Pts | Protocol / surface |
|---|---|---|---|---|---|
| 01 | Tripping Breaker | Offensive | Medium | 150 | IEC 60870‑5‑104 (:2404) |
| 02 | Safety Instrumented Bypass | Offensive | Hard | 250 | TriStation‑lite (:1502) |
| 03 | OPC Scout | Offensive | Medium | 150 | OPC‑UA‑lite (:9005) |
| 04 | Rogue Radio | Offensive | Hard | 200 | Custom pump TCP (:7700) |
| 06 | Second Blackout | Offensive | Expert | 300 | GOOSE training (UDP :6180) |
| 11 | Stabilise the Reactor | Offensive | Expert | 300 | Modbus/TCP (:502) |
| 12 | Maintenance Override | Offensive | Expert | 300 | Modbus/TCP (:502) |
| 07 | Historian SQL Injection | Defensive | Medium | 150 | Web (`/hist`, `/intranet`) |
| 08 | Field Device Recovery | Defensive | Hard | 350 | SNMP (UDP :161) |
| 09 | Trojan Historian | Defensive | Hard | 250 | Web (`/mail`, `/investigate`) |
| 10 | Device Takeover | Defensive | Hard | 100 | PROFINET DCP (saved evidence) |

**Total attainable: 2500 points** (7 offensive / 4 defensive).

## Documentation

- [`Devices/vanguard/docs/BADGE_PROVISIONING.md`](Devices/vanguard/docs/BADGE_PROVISIONING.md) — assign per‑badge SSID/identity.
- [`Devices/vanguard/docs/player-guide/ENVIRONMENT.md`](Devices/vanguard/docs/player-guide/ENVIRONMENT.md) — join the badge Wi‑Fi / set up your attack host.
- [`Devices/vanguard/docs/player-guide/TROUBLESHOOTING.md`](Devices/vanguard/docs/player-guide/TROUBLESHOOTING.md) — common issues.
- [`Devices/vanguard/tools/sysrupt/`](Devices/vanguard/tools/sysrupt/) — toolkit `README.md`, `COMMANDS.md`, `INSTALL.md`.

## Security & authorization

This is a **training/CTF simulator**. The "vulnerabilities," plaintext credentials, and insecure
defaults are **intentional fixtures** confined to the simulated plants on the badge. Use it **only**
against the authorized badge/lab — never against real industrial equipment. Challenge **walkthroughs,
answer keys, and solve guides are deliberately NOT in this repository** so a participant cannot obtain
answers by browsing it. (Organizers maintain those separately.)

## Known limitations

- No PSRAM: the firmware runs with a tight internal‑heap budget; the UI is tuned accordingly.
- Protocol implementations are **training simulations** of the real protocols, not full stacks.
- The host tests cover the simulation/protocol logic, not on‑device LCD rendering.
- Toolkit tested on **Linux + Python 3.9+**; Windows/macOS are not claimed.

## License & attribution

This project is a fork of **Tactility OS** (see [`TACTILITY_UPSTREAM.md`](TACTILITY_UPSTREAM.md) and
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)). SYSRUPT sources carry `SPDX‑License‑Identifier:
Apache‑2.0` headers; the repository‑level [`LICENSE.md`](LICENSE.md) and the third‑party component
licenses also apply. **Confirm the effective license of the combined work before public release** — the
maintainers have not finalized a single project‑wide license here.
