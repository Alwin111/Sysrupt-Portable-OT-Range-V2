# Contributing to SYSRUPT OT Range V2

Thanks for your interest in improving the range. This repository is a **fork of
[Tactility OS](https://tactilityproject.org)**: the SYSRUPT badge is a Tactility *device* under
[`Devices/vanguard/`](Devices/vanguard/). Please read [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) before
participating.

## Where a change belongs

- **SYSRUPT range code** (challenges, simulated plants, protocol servers, LCD challenge UI, scoring,
  the `sysrupt` toolkit) → this repository, under `Devices/vanguard/`.
- **Core Tactility / OS, drivers, or platform** behavior → generally belongs **upstream** at the
  [Tactility project](https://github.com/TactilityProject/Tactility); open the discussion there. Keep
  this fork focused on the range.

## Development environment

See [`Devices/vanguard/docs/FIRMWARE_DEVELOPMENT.md`](Devices/vanguard/docs/FIRMWARE_DEVELOPMENT.md) for
the full setup. In short:

- ESP‑IDF **v6.1** (simplest via the `espressif/idf:v6.1` Docker image; install `lark` in the build step).
- A C++17 host compiler for the C++ tests.
- Python **3.9+** for the toolkit (standard library only).

```bash
git clone --recursive <repo-url>          # submodules are required
# or: git submodule update --init --recursive
```

## Build and test workflow

Run the full host verification before opening a pull request — none of it needs a badge:

```bash
# C++ host tests (20 suites / 881 checks):
( cd Devices/vanguard/test && ./run_all.sh )

# Python toolkit tests (132 tests):
( cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q )

# Firmware build:
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
```

See [`Devices/vanguard/docs/TESTING.md`](Devices/vanguard/docs/TESTING.md) for the complete set.

## Source‑code conventions

- SYSRUPT sources carry an `SPDX-License-Identifier: Apache-2.0` header; keep it on new files.
- Match the surrounding style. The repository also ships Tactility's
  [`CODING_STYLE_C.md`](CODING_STYLE_C.md) / [`CODING_STYLE_CPP.md`](CODING_STYLE_CPP.md).
- Mind the **no‑PSRAM heap budget**: keep per‑challenge text, UI panels and web‑handler buffers small.

## Adding a challenge

1. Add a `ChallengeDef` entry to the correct array in
   [`Devices/vanguard/source/ui/Challenges.h`](Devices/vanguard/source/ui/Challenges.h) — the catalogue
   is the single source of truth.
2. Implement the backend under `source/ot/` (a simulated plant in `sim/` plus a `Service` wrapper) and
   derive its "solved" state from the plant.
3. Keep the invariants intact, or update them deliberately: the `static_assert`s require
   `SCORE_MAX == 2500` and exactly 11 challenges, and `ot_catalogue_tests` checks the 7/4 split. If you
   change the catalogue, update the points so the total still holds (or change the asserts on purpose).
4. Add host tests under `test/` and register them in `test/run_all.sh`.
5. Keep flags and answer keys out of documentation.

## Modifying the toolkit

1. Commands are defined in
   [`Devices/vanguard/tools/sysrupt/sysrupt/registry.py`](Devices/vanguard/tools/sysrupt/sysrupt/registry.py)
   — the single source of truth.
2. Add/adjust tests under `tools/sysrupt/tests/`.
3. **Regenerate the generated artifacts** (see below) — the embedded‑ZIP test will fail otherwise.

## Regenerating embedded toolkit assets

Any toolkit change must be followed by:

```bash
cd Devices/vanguard/tools/sysrupt
python3 make_release.py                                       # -> release/sysrupt-toolkit-1.0.0.zip
python3 gen_embedded_zip.py release/sysrupt-toolkit-1.0.0.zip # -> ../../source/ot/net/ToolkitZip.{h,cpp}
python3 gen_commands.py                                       # -> COMMANDS.md
```

`ot_toolkitzip_tests` asserts the embedded bytes equal the release ZIP byte‑for‑byte; commit the
regenerated `ToolkitZip.{h,cpp}`, the release ZIP, and `COMMANDS.md` together.

## Updating documentation

SYSRUPT docs live under [`Devices/vanguard/docs/`](Devices/vanguard/docs/). Keep them consistent with
the source (ports, challenge numbers and the points table come from `Challenges.h` and the service
code). Do not add flags, credentials or solution steps to any committed documentation.

## Pull request checklist

- [ ] C++ host tests pass (`test/run_all.sh`).
- [ ] Python toolkit tests pass (`pytest`).
- [ ] Firmware builds with ESP‑IDF v6.1.
- [ ] Scoring invariants intact (11 challenges, 2500 points, 7 offensive / 4 defensive) — or changed on
      purpose with the `static_assert`s updated.
- [ ] If the toolkit changed: embedded ZIP + `COMMANDS.md` regenerated and committed.
- [ ] No flags, credentials, or solution steps added to documentation.
- [ ] New source files carry the SPDX license header.
- [ ] The PR is a single, related set of changes.

## Licensing of contributions

This repository combines GPL‑3.0 (Tactility base) and Apache‑2.0 (SYSRUPT `Devices/vanguard/`) code; see
[`LICENSE.md`](LICENSE.md), the SPDX headers, and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
Contributions are accepted under the license of the component you are changing. When third‑party code is
used, its license must be included and must be compatible with the component it joins. By contributing
you agree your changes are licensed accordingly.

---

*Contributions to upstream Tactility are governed by the
[Tactility project's own contribution process](https://tactilityproject.org/#/support).*
