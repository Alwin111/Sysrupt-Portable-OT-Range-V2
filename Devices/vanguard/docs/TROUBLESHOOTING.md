# Troubleshooting (build & development)

Practical fixes for issues confirmed from the source tree and build. For **player‑side** issues
(joining the badge Wi‑Fi, reaching `10.0.1.1`, downloading the toolkit), see
[player-guide/TROUBLESHOOTING.md](player-guide/TROUBLESHOOTING.md).

## Dependencies / checkout

| Symptom | Likely cause | Fix |
|---|---|---|
| `Libraries/<name>` is empty; headers not found | submodules not initialized | `git submodule update --init --recursive` |
| `mbedtls` can't find `framework` / `tf-psa-crypto` | nested submodules missing | re‑run the recursive init above (`--recursive` is required) |
| Submodule checkout points at an unexpected commit | pins drifted | compare against [`SUBMODULES.md`](../../../SUBMODULES.md); `git -C Libraries/<name> checkout <pin>` |

## Firmware build

| Symptom | Likely cause | Fix |
|---|---|---|
| `ModuleNotFoundError: No module named 'lark'` | the IDF image lacks `lark` | run `pip install lark` inside the build step |
| Link error for a file you just added | `GLOB_RECURSE` source cache | `idf.py reconfigure` then `idf.py build` |
| Managed components re‑download each build | `managed_components/` is git‑ignored (by design) | normal; they come from `dependencies.lock` |
| Build succeeds but binary larger than expected | added large assets/strings | the app partition is 6 MB; keep per‑challenge text small (no‑PSRAM budget) |

## Serial / flashing (operator)

> This repository's documentation does not flash the badge; these notes are for operators who do.

| Symptom | Likely cause | Fix |
|---|---|---|
| Badge not detected | wrong port / cable | check `/dev/ttyACM0` (Linux); try another USB‑C cable/port |
| Flash fails midway | baud/flash‑mode mismatch | use `flash_badge.sh` (DIO, 16 MB) rather than hand‑rolled esptool flags |
| Progress lost after reflash | used `--erase` | omit `--erase` to preserve NVS; it is preserved by default |

## Boot / runtime (on device)

| Symptom | Likely cause | Fix |
|---|---|---|
| `MemoryChecker: Internal memory low` / crash when opening a view | no‑PSRAM heap exhaustion | reduce resident UI; keep only one heavy panel built at a time; shrink challenge story/hints and web‑handler buffers |
| A challenge never shows "complete" | solved state comes from the live backend | confirm the action actually changed plant state (use the toolkit's verification ladder) |
| Services unreachable right after boot | AP/services still coming up | `startAllServices()` runs once at boot; give it a moment, then re‑probe |

## LCD rendering

| Symptom | Likely cause | Fix |
|---|---|---|
| Colors look inverted / mirrored after a driver change | ST7789 orientation flags | the panel uses `swap-xy` + `mirror-x` + `invert-color` (see [HARDWARE.md](HARDWARE.md)); don't change these without re‑testing on hardware |
| Text clipped in list/breadcrumb | long title | a short `displayTitle` is used where space is tight; keep it short |

> On‑device LCD appearance can only be confirmed by looking at a flashed badge — there is no automated
> visual check.

## Challenge navigation

| Symptom | Likely cause | Fix |
|---|---|---|
| BACK/OK behave oddly | nav vs. system key roles | nav keys go to LVGL; S1–S4 system keys are `emit-only` (S1 PAUSE / S3 START are detected but unassigned) — see [HARDWARE.md](HARDWARE.md) |

## Toolkit download / Python

| Symptom | Likely cause | Fix |
|---|---|---|
| `http://10.0.1.1/toolkit.zip` fails | host not on the badge Wi‑Fi | join `SYSRUPT-OT-V2-NNN` first (see [player-guide/ENVIRONMENT.md](player-guide/ENVIRONMENT.md)) |
| `sysrupt` won't run | wrong Python | needs Python **3.9+**; it uses only the standard library |
| `pytest` not found | pytest not installed | `pip install pytest` (only needed to run the tests, not the toolkit) |

## Test failures

| Symptom | Likely cause | Fix |
|---|---|---|
| `ot_toolkitzip_tests` fails | toolkit changed, embedded asset stale | re‑run `gen_embedded_zip.py` after `make_release.py` |
| `COMMANDS.md` diff after `gen_commands.py` | doc out of date | commit the regenerated `COMMANDS.md` |
| A catalogue/invariant test fails | challenge count/points/split changed | restore the invariants (11 challenges, 2500, 7/4) or update intentionally with the `static_assert`s |
