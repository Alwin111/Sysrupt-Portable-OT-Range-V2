# Vanguard device — SYSRUPT OT Range V2

This directory is the **SYSRUPT OT Range V2** badge firmware, implemented as a Tactility OS *device*
(ESP32‑S3 "Vanguard"). It is a self‑contained OT/ICS CTF: the badge boots its own Wi‑Fi access point
(`SYSRUPT‑OT‑V2‑NNN`, gateway `10.0.1.1`) and simulates a small industrial site with **11 challenges**.

See the repository‑root [`README.md`](../../README.md) for the project overview, build / flash / test
instructions, the challenge catalogue, and the player toolkit.

## Layout
| Path | Contents |
|---|---|
| `source/ot/sim/` | Simulated plants (reactor, pump, historian, substation, SIS, SNMP device, …) |
| `source/ot/net/` | Protocol servers + the `:80` web server (OtHttp / DefWeb / HistWeb, `/toolkit.zip`) |
| `source/ui/` | LCD UI: challenge registry, views, scoring + NVS persistence |
| `test/` | Host‑side C++ unit tests — `./test/run_all.sh` |
| `tools/sysrupt/` | Player‑side `sysrupt` CLI toolkit + release packaging |
| `docs/` | Provisioning, player environment, troubleshooting |

## Quick links
- Join the badge / set up your host: [`docs/player-guide/ENVIRONMENT.md`](docs/player-guide/ENVIRONMENT.md)
- Assign a per‑badge identity: [`docs/BADGE_PROVISIONING.md`](docs/BADGE_PROVISIONING.md)
- Toolkit: [`tools/sysrupt/README.md`](tools/sysrupt/README.md)

> This is a training/CTF simulator; its intentional vulnerabilities and fixtures are confined to the
> badge. Challenge walkthroughs and answer keys are kept out of this public repository.
