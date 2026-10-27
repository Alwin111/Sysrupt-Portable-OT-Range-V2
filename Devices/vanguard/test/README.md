# `test/` — host-side unit tests

Pure C++ tests that run on your laptop (no ESP32, no hardware). They cover the simulated plants and the
protocol logic in [`../source/ot/`](../source/ot/).

## Run them

```bash
cd Devices/vanguard/test
./run_all.sh
```

Expected: **20 suites / 881 checks**, all passing. Each suite is a standalone `ot_*_tests.cpp` compiled
with `g++` (C++17) and run directly.

## What's covered

Modbus, IEC-104, TriStation/SIS, OPC-UA-lite, pump protocol, GOOSE, SNMP, historian + SQLi (incl. UNION),
the reactor/plant physics, substation, the scoring catalogue invariants, the session/NVS model, and the
embedded-toolkit-ZIP byte-for-byte check (`ot_toolkitzip_tests`).

## Adding a test

Add an `ot_<area>_tests.cpp` that exits non-zero on failure and prints an `OK`/count line; `run_all.sh`
discovers and runs each suite and aggregates the totals. Keep tests hardware-free — they must build and
run anywhere `g++` does.

The Python toolkit has its own suite: `cd ../tools/sysrupt && python3 -m pytest -q` (132 tests).
