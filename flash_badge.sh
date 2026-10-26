#!/usr/bin/env bash
# flash_badge.sh - flash this built Vanguard OT Range V2 firmware onto an ESP32-S3 badge.
#
# Usage:
#   ./flash_badge.sh                 # auto-detect the serial port and flash
#   ./flash_badge.sh -p /dev/ttyACM0 # force a specific port
#   ./flash_badge.sh -b 921600       # faster baud (default 460800)
#   ./flash_badge.sh --erase         # full chip erase first (clean slate; wipes everything incl. NVS)
#   ./flash_badge.sh --monitor       # open the serial monitor after flashing (Ctrl-] / Ctrl-C to quit)
#   ./flash_badge.sh -y              # don't ask for confirmation
#
# It flashes the artifacts already built in ./build (bootloader, partition table, app, system, data).
# Prefers the Espressif Docker image (the exact tool this firmware was built/flashed with); falls back
# to a host `esptool` if Docker is unavailable. By default it does NOT touch the `nvs` partition, so a
# badge's saved session/score survives a reflash; use --erase for a truly fresh device.
set -euo pipefail

CHIP=esp32s3
BAUD=460800
PORT=""
ERASE=0
MONITOR=0
ASSUME_YES=0
IMG=espressif/idf:v6.1

# --- resolve the build directory (next to this script) ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# --- parse args ---
while [ $# -gt 0 ]; do
  case "$1" in
    -p|--port)    PORT="$2"; shift 2;;
    -b|--baud)    BAUD="$2"; shift 2;;
    --erase)      ERASE=1; shift;;
    --monitor)    MONITOR=1; shift;;
    -y|--yes)     ASSUME_YES=1; shift;;
    -h|--help)    grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'; exit 0;;
    *) echo "unknown option: $1 (try --help)"; exit 2;;
  esac
done

# --- sanity: build artifacts present ---
need=(bootloader/bootloader.bin partition_table/partition-table.bin Tactility.bin system.bin data.bin flash_args)
for f in "${need[@]}"; do
  if [ ! -f "$BUILD_DIR/$f" ]; then
    echo "ERROR: missing build artifact: build/$f"
    echo "Build the firmware first (from $SCRIPT_DIR):"
    echo "  docker run --rm -v \"\$PWD\":/project -w /project $IMG idf.py build"
    exit 1
  fi
done

# --- detect the serial port if not given ---
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1 || true)"
  if [ -z "$PORT" ]; then
    echo "ERROR: no serial port found (/dev/ttyACM* or /dev/ttyUSB*). Plug the badge in, or pass -p."
    exit 1
  fi
fi

echo "============================================================"
echo " Vanguard OT Range V2 - flash to badge"
echo "   firmware : $BUILD_DIR"
echo "   chip     : $CHIP"
echo "   port     : $PORT"
echo "   baud     : $BAUD"
echo "   erase    : $([ $ERASE -eq 1 ] && echo 'YES (full chip erase, wipes NVS/session too)' || echo 'no (keeps NVS; reflash preserves saved session)')"
echo "============================================================"
echo "NOTE: this installs THIS firmware and overwrites the badge's app + /data."
if [ $ASSUME_YES -ne 1 ]; then
  read -r -p "Proceed? [y/N] " a; [ "$a" = "y" ] || [ "$a" = "Y" ] || { echo "aborted."; exit 1; }
fi

# --- pick a flashing backend ---
use_docker=0
if command -v docker >/dev/null 2>&1 && docker image inspect "$IMG" >/dev/null 2>&1; then
  use_docker=1
fi

run_docker() {   # args: subcommand...
  docker run --rm --device="$PORT" -v "$SCRIPT_DIR":/project -w /project/build "$IMG" \
    bash -c "python -m esptool --chip $CHIP -p $PORT -b $BAUD $*"
}

flash_docker() {
  [ $ERASE -eq 1 ] && run_docker "erase-flash"
  run_docker "--before default-reset --after hard-reset write-flash @flash_args"
}

flash_host() {
  # esptool v4.x uses underscore arg/subcommand names; v5.x uses hyphens. Detect the major version.
  local ver major w before after fmode fsize ffreq
  ver="$(esptool version 2>/dev/null | grep -oE 'v[0-9]+' | head -1 | tr -d v || echo 4)"
  major="${ver:-4}"
  if [ "$major" -ge 5 ]; then
    w=write-flash; before=default-reset; after=hard-reset; fmode=--flash-mode; fsize=--flash-size; ffreq=--flash-freq
    local era=erase-flash
  else
    w=write_flash; before=default_reset; after=hard_reset; fmode=--flash_mode; fsize=--flash_size; ffreq=--flash_freq
    local era=erase_flash
  fi
  ( cd "$BUILD_DIR"
    [ $ERASE -eq 1 ] && esptool --chip "$CHIP" -p "$PORT" "$era"
    esptool --chip "$CHIP" -p "$PORT" -b "$BAUD" --before "$before" --after "$after" \
      "$w" $fmode dio $fsize 16MB $ffreq 80m \
      0x0 bootloader/bootloader.bin \
      0x8000 partition_table/partition-table.bin \
      0x10000 Tactility.bin \
      0x610000 system.bin \
      0x640000 data.bin
  )
}

if [ $use_docker -eq 1 ]; then
  echo ">> flashing via Docker ($IMG)"
  flash_docker
elif command -v esptool >/dev/null 2>&1; then
  echo ">> flashing via host esptool ($(esptool version 2>/dev/null | head -1))"
  flash_host
else
  echo "ERROR: neither Docker image '$IMG' nor a host 'esptool' is available."
  echo "Install esptool:  pip install esptool    (or pull the Docker image)"
  exit 1
fi

echo ""
echo "DONE. The badge should reboot into the firmware and bring up WiFi 'SYSRUPT-OT-V2' (10.0.1.1)."

if [ $MONITOR -eq 1 ]; then
  echo ">> opening serial monitor (115200). Ctrl-C to quit."
  if command -v python3 >/dev/null 2>&1 && python3 -c "import serial" 2>/dev/null; then
    python3 -c "import serial,sys; s=serial.Serial('$PORT',115200); [sys.stdout.write(s.readline().decode('utf-8','replace')) or sys.stdout.flush() for _ in iter(int,1)]"
  else
    echo "(install pyserial for the monitor:  pip install pyserial  - or use: screen $PORT 115200)"
  fi
fi
