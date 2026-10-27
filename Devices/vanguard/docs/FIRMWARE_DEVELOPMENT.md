# Firmware development guide

How to fetch dependencies, build the firmware, run the host tests, and (as documentation only) flash a
badge. Commands are shown exactly; paths are relative to the repository root unless noted.

## Toolchain

| Tool | Version | Notes |
|---|---|---|
| ESP‑IDF | **v6.1** (pinned) | Simplest via the official Docker image `espressif/idf:v6.1` |
| Python (build helper) | provided by the IDF image | the image lacks `lark`; install it in the build step |
| Host C++ compiler | C++17 (`g++`) | for `test/run_all.sh`; no ESP hardware needed |
| Python (toolkit) | 3.9+ | standard library only |

## Get the source (submodules)

The libraries under `Libraries/` are **git submodules** pinned to specific revisions (see
[`SUBMODULES.md`](../../../SUBMODULES.md)). Clone recursively, or initialize after cloning:

```bash
git clone --recursive <repo-url>
# or, after a plain clone:
git submodule update --init --recursive
```

`--recursive` matters: `mbedtls` has its own nested submodules that its build needs.

## Build the firmware

```bash
# from the repository root, using the pinned ESP-IDF toolchain via Docker:
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
```

- The IDF image does not ship `lark`, so install it in the build step (as above).
- Managed components are fetched from the committed `dependencies.lock` on first build.
- Output: `build/Tactility.bin` (≈ `0x3266b0`, ~3.3 MB; app partition is `0x600000` / 6 MB, ~47% free).

> **Adding a new source file:** the build globs sources recursively (`GLOB_RECURSE`), so a newly added
> `.cpp`/`.c` needs a reconfigure before it is picked up:
> ```bash
> docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
>   bash -c "pip install lark && idf.py reconfigure && idf.py build"
> ```

## Run the host tests

No hardware required.

```bash
# C++ host unit tests (simulation + protocol logic):
cd Devices/vanguard/test && ./run_all.sh          # 20 suites / 881 checks

# Python toolkit tests:
cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q   # 132 tests
```

See [TESTING.md](TESTING.md) for the full verification set.

## Flash a badge (documentation only)

> These commands are documented for operators. This guide does not run them, and flashing is outside
> the scope of a documentation change.

```bash
# from the repository root (preserves NVS / saved progress):
./flash_badge.sh -p /dev/ttyACM0 -y
# full wipe (clears saved progress):
./flash_badge.sh -p /dev/ttyACM0 -y --erase
```

The build also prints the raw `esptool` write‑flash command with every partition offset if you prefer
to flash manually.

## Debugging

- **Serial monitor:** attach to the badge's USB serial (`/dev/ttyACM0` on Linux) to read boot logs and
  the heap/`MemoryChecker` output. The no‑PSRAM heap budget is tight; watch for `Internal memory low`
  warnings when editing UI views.
- **Host‑first:** the simulated plants and protocol logic are host‑testable, so reproduce and fix logic
  bugs in `test/` before flashing.
- **MAC / identity:** `esptool --chip esp32s3 -p /dev/ttyACM0 read_mac` reads the badge MAC, which maps
  to its provisioned SSID (`SYSRUPT-OT-V2-NNN`). See [BADGE_PROVISIONING.md](BADGE_PROVISIONING.md).

## Common build issues

| Symptom | Cause | Fix |
|---|---|---|
| `ModuleNotFoundError: lark` | IDF image lacks `lark` | run `pip install lark` in the build step |
| Link error referencing a newly added file | `GLOB_RECURSE` cache | `idf.py reconfigure` then `idf.py build` |
| Empty `Libraries/<name>` / missing headers | submodules not initialized | `git submodule update --init --recursive` |
| `mbedtls` build can't find `framework`/`tf-psa-crypto` | nested submodules missing | re‑run the recursive init above |
| Managed components re‑download every build | `managed_components/` is git‑ignored (expected) | they are fetched from `dependencies.lock`; this is normal |

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for runtime/on‑device issues.
