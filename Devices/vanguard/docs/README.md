# SYSRUPT OT Range V2 — Documentation

Engineering and player documentation for the SYSRUPT OT Range V2 badge firmware. All SYSRUPT code
lives under [`Devices/vanguard/`](../); this directory is its documentation hub.

| Document | Audience | Contents |
|---|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Developers | Firmware structure, challenge registry/dispatch, scoring, NVS, web server, embedded toolkit, tests |
| [HARDWARE.md](HARDWARE.md) | Hardware / firmware | ESP32‑S3 config, ST7789 display, input keypad, flash/storage, serial, verified pin map, limitations |
| [FIRMWARE_DEVELOPMENT.md](FIRMWARE_DEVELOPMENT.md) | Developers | Toolchain, dependency init, build/test/flash commands, debugging, common build issues |
| [TOOLKIT.md](TOOLKIT.md) | Players / developers | The `sysrupt` Python CLI: layout, modules, usage, commands, packaging, embedded‑ZIP checks |
| [CHALLENGES.md](CHALLENGES.md) | Players / organizers | Spoiler‑free catalogue of the 11 challenges (IDs, names, category, points, objectives) |
| [TESTING.md](TESTING.md) | Contributors | C++ host tests, Python tests, invariant/packaging checks, how to reproduce verification |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Everyone | Build, serial, boot, LCD, navigation, toolkit download, Python env, test failures |
| [DEVELOPMENT_STATUS.md](DEVELOPMENT_STATUS.md) | Everyone | What is verified, partial, hardware‑dependent, or unverified |

## Player quick‑start documents

- [player-guide/ENVIRONMENT.md](player-guide/ENVIRONMENT.md) — join the badge Wi‑Fi, set up an attack host.
- [player-guide/TROUBLESHOOTING.md](player-guide/TROUBLESHOOTING.md) — player‑facing issues.
- [BADGE_PROVISIONING.md](BADGE_PROVISIONING.md) — assign a per‑badge SSID / identity.

## Where documentation lives

SYSRUPT docs are co‑located with the device under `Devices/vanguard/docs/` (not the repository‑root
`Documentation/`, which belongs to upstream Tactility). This keeps SYSRUPT material next to the code it
documents and avoids mixing it with upstream attribution. The repository‑root
[`README.md`](../../../README.md) is the project landing page and links here.
