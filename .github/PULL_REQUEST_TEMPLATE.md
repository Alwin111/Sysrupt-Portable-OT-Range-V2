<!-- Thanks for contributing to SYSRUPT OT Range V2. Keep a PR to one related set of changes. -->

## Summary

<!-- What does this change and why? -->

## Area

- [ ] Challenge / simulated plant / protocol server
- [ ] LCD UI / scoring / persistence
- [ ] `sysrupt` toolkit
- [ ] Documentation
- [ ] Build / CI / tooling

## Verification (host — no badge required)

- [ ] C++ host tests pass — `cd Devices/vanguard/test && ./run_all.sh`
- [ ] Python toolkit tests pass — `cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q`
- [ ] Firmware builds — `espressif/idf:v6.1` + `idf.py build`

## Invariants

- [ ] 11 challenges, 2500 points, 7 offensive / 4 defensive — intact, or changed on purpose with the
      `static_assert`s updated
- [ ] If the toolkit changed: embedded ZIP + `COMMANDS.md` regenerated and committed
      (`make_release.py` → `gen_embedded_zip.py` → `gen_commands.py`)

## Safety / public-repo hygiene

- [ ] No flags, credentials, or solution steps added to documentation
- [ ] New source files carry `SPDX-License-Identifier`

## On-hardware testing (if any)

<!-- State exactly what you tested on a physical badge (badge id, what you observed). Leave blank if
you did not test on hardware — do not imply hardware testing that did not happen. -->
