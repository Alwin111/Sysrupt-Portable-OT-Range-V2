# Development status

An honest account of what is verified versus what is not, so nobody relies on a claim that was never
tested. "Verified (host, this release)" means it was run on a development host during release
preparation. A **limited on‑device boot test** (serial only) was additionally performed on one badge —
see the Hardware‑dependent section below; LCD visuals and gameplay remain unverified.

## ✅ Verified (host, this release)

| Area | Evidence |
|---|---|
| Firmware builds with the pinned toolchain | `espressif/idf:v6.1` + `pip install lark` + `idf.py build` → `Tactility.bin` `0x3266b0`, app partition `0x600000`, ~47% free, exit 0 |
| C++ host test suite | `test/run_all.sh` → **20 suites / 881 checks / 0 failed** |
| Python toolkit test suite | `pytest` → **132 passed** |
| Scoring invariants | compile‑time `static_assert`s (SCORE_MAX = 2500; 11 challenges) + `ot_catalogue_tests` (7 offensive / 4 defensive) |
| Embedded toolkit ZIP integrity | `ot_toolkitzip_tests`; embedded bytes == release ZIP, SHA‑256 `4361fd88…77070a` |
| Generated command docs | `gen_commands.py` reproduces `COMMANDS.md` with no diff |
| Dependencies resolve from a clean checkout | submodules initialize recursively; managed components fetch from `dependencies.lock` |

## 🟡 Partial / simulated by design

| Area | Status |
|---|---|
| Protocol servers (Modbus, IEC‑104, TriStation‑lite, OPC‑UA‑lite, GOOSE, SNMP) | **Training simulations**, not full standard stacks — see [ARCHITECTURE.md](ARCHITECTURE.md#protocol-realism) |
| Historian SQL injection | Bounded pattern‑matching simulator, intentionally **not** a real SQL engine |
| GOOSE | GOOSE‑shaped exercise over **UDP 6180**, not raw Layer‑2 multicast |
| Challenge #05 (Dial‑In Dam) | Removed from the live catalogue; code retained as reference, not started |

## 🔌 Hardware‑dependent

A limited on‑device **boot** test was performed (serial only); LCD visuals and gameplay were not.

### Verified on device (2026‑10‑26, badge `ac:27:6e:c6:f1:44`, via serial boot log)

A flash + boot was performed on one physical badge. Evidence is the serial log, not a visual check:

| Area | Evidence |
|---|---|
| Flashing | `flash_badge.sh` wrote the app at `0x10000`, **hash verified**, NVS preserved (no `--erase`) |
| Clean boot | boot reached the app; `Heap: 151420 / 328396 available` (~151 KB free) |
| Wi‑Fi AP | `RangeNet: OT range AP up: SSID=SYSRUPT-OT-V2-44, badge=10.0.1.1`; DHCP server running |
| Service bring‑up | web HMI `/`, `/intranet`, historian `/hist`; listeners on 502, 1502, 2404, 6180 (UDP), 7700, 9005, 161 (UDP) — all logged up, matching the documented ports |
| Controls task | `Controls enabled (joystick = navigate, BACK = back, DISP = reset session)` |

### Still NOT verified (require a human at the badge)

| Area | Status |
|---|---|
| On‑device LCD **visual** rendering / layout | **Unverified** — no camera/visual channel; only serial was observed |
| Menu navigation / challenge views / completion screen on the LCD | **Unverified** — needs a person watching the screen |
| End‑to‑end challenge solving + scoring on hardware | **Unverified** — not played through on the badge |
| Web page **content** at `10.0.1.1` | **Unverified** — the test host was not joined to the badge AP |
| NVS persistence of a solve across reboot | **Unverified** — NVS was preserved, but persist‑then‑reboot was not exercised |
| Input keypad **behavior** (beyond the controls task starting) | **Unverified** — pin map documented from the devicetree |
| Unconfigured peripherals (LoRa SX1261, LED shift registers, buzzer, status LED, battery sense) | **Not implemented** in firmware |

## ❔ Unverified / unknown

- Toolkit tested on **Linux + Python 3.9+**; Windows/macOS are not claimed.
- Panel diagonal (2.4″) is assumed in `device.properties`; it only affects UI scaling.
- The device descriptor is marked `incubating=true`.

## How to raise an item out of "unverified"

On‑device items move to verified only when someone flashes a badge and observes the behavior directly
(LCD, navigation, live protocols, persistence). Record what was tested, on which badge, and when —
don't mark hardware behavior verified on the strength of host tests alone.
