# Testing and quality assurance

All verification below runs on a development host — **no ESP32 hardware is required**. On‑device LCD
rendering and live‑network behavior are *not* covered by these tests (see
[hardware‑in‑the‑loop](#what-these-tests-do-not-cover)).

## C++ host test suite

Covers the simulated plants and protocol logic.

```bash
cd Devices/vanguard/test && ./run_all.sh
```

- Compiles each suite with the host `g++` (C++17) and runs it.
- Current baseline: **20 suites, 881 checks, 0 failed.**
- Includes the catalogue/invariant suite (`ot_catalogue_tests`) and the embedded‑ZIP integrity suite
  (`ot_toolkitzip_tests`).

## Python toolkit test suite

```bash
cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q
```

- Standard‑library only; Python 3.9+.
- Current baseline: **132 passed.**

## Challenge / scoring invariant checks

The scoring catalogue is guarded at compile time, so any change that breaks an invariant fails the
build and the host suite:

```cpp
static_assert(SCORE_MAX == 2500, ...);               // total attainable points
static_assert(CHALLENGE_COUNT + DEFENSIVE_COUNT == 11, ...);
```

`ot_catalogue_tests` additionally asserts the **7 offensive / 4 defensive** split and the per‑challenge
points.

## Toolkit packaging integrity

```bash
cd Devices/vanguard/tools/sysrupt
python3 make_release.py                                      # deterministic release ZIP
python3 gen_embedded_zip.py release/sysrupt-toolkit-1.0.0.zip # regenerate ToolkitZip.{h,cpp}
python3 gen_commands.py                                      # regenerate COMMANDS.md
```

- `ot_toolkitzip_tests` asserts the firmware‑embedded bytes equal the release ZIP **byte‑for‑byte**
  (SHA‑256 `4361fd88…77070a`).
- `gen_commands.py` regenerates `COMMANDS.md` from the registry; re‑running it should produce no diff
  (a diff means `COMMANDS.md` is stale).

## Firmware build validation

A successful firmware build is itself a check (the invariants are `static_assert`s):

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
```

Current baseline: `build/Tactility.bin` ≈ `0x3266b0`, app partition `0x600000`, ~47% free.

## Static public‑documentation checks

Before publishing, confirm no answer keys leak into documentation:

```bash
# flags must appear ONLY in source/tests, never in docs:
grep -rIl --exclude-dir=.git --exclude-dir=Libraries 'flag{' . \
  | grep -iE '\.(md|html?|pdf|txt|rst)$'   # expect: no output
```

## Reproducing the full verification

```bash
# 1. dependencies
git submodule update --init --recursive
# 2. C++ host tests
( cd Devices/vanguard/test && ./run_all.sh )
# 3. Python tests
( cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q )
# 4. firmware build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
```

## What these tests do NOT cover

- **On‑device LCD rendering** — there is no automated pixel/visual check in CI. Any claim about LCD
  layout must come from a human looking at a flashed badge.
- **Live network behavior on hardware** — the host tests exercise the plant/protocol *logic*, not a
  running badge AP. Hardware‑in‑the‑loop testing is manual.
- See [DEVELOPMENT_STATUS.md](DEVELOPMENT_STATUS.md) for what is and isn't verified.
