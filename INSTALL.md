# Installation Guide

This guide takes you from a fresh clone to a running badge.

> **Time required:** ~15 minutes (most of it is the first firmware build)
> **You need:** an ESP32-S3 "Vanguard" badge, a USB-C cable, and Docker (for the build) **or** a
> pre-built `build/Tactility.bin`.

---

## Option A — Flash a pre-built firmware (fastest)

If `build/Tactility.bin` (and the other `build/*.bin` artifacts) are already present — from a release or
a previous build — just flash:

```bash
./flash_badge.sh -p /dev/ttyACM0 -y
```

- `-p /dev/ttyACM0` — the badge's USB serial port (check with `ls /dev/ttyACM* /dev/ttyUSB*`).
- `-y` — don't prompt for confirmation.
- The flasher **preserves NVS** (your saved challenge progress) by default. Add `--erase` for a clean
  wipe.

The badge reboots, brings up its Wi-Fi AP `SYSRUPT-OT-V2-…`, and serves the HMI at `http://10.0.1.1/`.
Jump to [Connecting a player laptop](#connecting-a-player-laptop).

---

## Option B — Build from source

### Before you start — read this

| Gotcha | Why it matters |
|--------|----------------|
| Use **ESP-IDF v6.1** (the `espressif/idf:v6.1` Docker image is simplest) | Other IDF versions are not tested and may not match `dependencies.lock`. |
| `pip install lark` **inside** the build container | The build's code-generation step needs it; without it the build fails early. |
| Clone **with submodules** | `Libraries/` are git submodules pinned to exact revisions ([`SUBMODULES.md`](SUBMODULES.md)). A plain clone misses them. |
| First build downloads managed components | It fetches from the committed `dependencies.lock`; it needs internet once. |
| The app partition is **6 MB**, the badge has **no PSRAM** | Keep added code/assets small; the build's `check_sizes` step enforces the partition limit. |

### 1. Get the source

```bash
git clone --recursive https://github.com/Alwin111/Sysrupt-Portable-OT-Range-V2.git
cd Sysrupt-Portable-OT-Range-V2
# if you already cloned without --recursive:
git submodule update --init --recursive
```

### 2. Build the firmware (Docker ESP-IDF)

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
```

Output: `build/Tactility.bin` (≈ 4 MB; the 6 MB app partition has ~35% free). The build ends with
`Project build complete.`

> **No Docker?** Install ESP-IDF v6.1 natively, run `. $IDF_PATH/export.sh`, then
> `pip install lark && idf.py build` from the repo root.

### 3. Run the tests (optional, no hardware needed)

```bash
# C++ host unit tests — 20 suites / 881 checks:
cd Devices/vanguard/test && ./run_all.sh

# Python toolkit tests — 132 tests:
cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q
```

### 4. Flash the badge

```bash
./flash_badge.sh -p /dev/ttyACM0 -y
```

After it prints `DONE`, the badge reboots into the range.

---

## Connecting a player laptop

1. On your laptop, join the badge's open Wi-Fi: **`SYSRUPT-OT-V2-…`**.
2. Your laptop gets an address in `10.0.1.x` automatically. The badge is **`10.0.1.1`**.
3. Open the operator HMI:
   ```bash
   firefox http://10.0.1.1/
   ```
4. Download the protocol toolkit from the badge and probe the ports (read-only):
   ```bash
   curl -O http://10.0.1.1/toolkit.zip
   unzip toolkit.zip && cd sysrupt-toolkit-1.0.0
   ./bin/sysrupt status
   ```

> **Be gentle with the badge.** It has no PSRAM and a tiny lwIP stack. Use **one connection at a time,
> low rate**. An aggressive `nmap -T4` sweep makes ports read *filtered* (the stack drops parallel
> probes) — that is not the same as "closed". Prefer `nmap -sT -T2 -p <specific ports>`.

---

## Verifying the install

```bash
# From the laptop, once joined to the badge Wi-Fi:
ping -c2 10.0.1.1                      # badge reachable
curl -s -o /dev/null -w '%{http_code}\n' http://10.0.1.1/   # 200 / 302
./bin/sysrupt status                   # lists the challenge ports that answered
```

On the badge itself, the USB serial console (`/dev/ttyACM0`, 115200) prints the boot log, including
lines like `OtHttp: Engineering HMI up on http://10.0.1.1/` and the protocol-listener ports.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Build fails early in a code-gen step | `lark` not installed in the container | Re-run with `pip install lark && idf.py build` |
| Build fails fetching components | No internet on first build | Connect, re-run `idf.py build` (then it's cached) |
| `check_sizes` fails: app too big | Added code/assets exceed the 6 MB partition | Shrink the change; the limit is fixed by the partition table |
| `flash_badge.sh`: no such port | Wrong `-p` value | `ls /dev/ttyACM* /dev/ttyUSB*`; pass the right one |
| Laptop never gets a `10.0.1.x` address | Joined the wrong SSID, or laptop runs its own DHCP/Internet-Sharing | Re-join `SYSRUPT-OT-V2-…`; disable Internet Sharing; reconnect |
| Ports show `filtered` in nmap | Parallel probes dropped by the tiny stack | Scan slowly, one port at a time (`-sT -T2 -p 502`) |
| HMI page is slow or stalls | Too many concurrent connections | Close extra tabs; one request at a time |
| Saved progress disappeared | Flashed with `--erase` | Omit `--erase` to preserve NVS; progress is in NVS |

More hardware/firmware detail: [`Devices/vanguard/docs/TROUBLESHOOTING.md`](Devices/vanguard/docs/TROUBLESHOOTING.md).

---

## Resetting between sessions

- **Keep the firmware, clear progress:** on the badge, use the **Reset Session** screen in the LCD menu.
- **Full wipe + re-flash:** `./flash_badge.sh -p /dev/ttyACM0 -y --erase`.

A plain reboot does **not** erase NVS — your scores and completions persist.

---

## Next steps

- **Players:** start at [`docs/index.md`](docs/index.md), then the spoiler-free
  [challenge guides](docs/challenges/).
- **Operators:** [`docs/internal/SETUP.md`](docs/internal/SETUP.md).
- **Developers:** [`docs/internal/ARCHITECTURE.md`](docs/internal/ARCHITECTURE.md) and the deep docs in
  [`Devices/vanguard/docs/`](Devices/vanguard/docs/).
