#!/usr/bin/env bash
# provision_badge.sh - assign a stable badge NUMBER (1..999) to a Vanguard badge.
#
# The number is stored in NVS (namespace "vgbadge", key "no"). After provisioning + a reboot the badge
# broadcasts Wi-Fi SSID  SYSRUPT-OT-V2-NNN  and shows  BADGE NNN  on its starting screen. A badge that
# is never provisioned still gets a UNIQUE, stable SSID derived from its factory MAC
# (SYSRUPT-OT-V2-XXXXXX, shown as "BADGE ID XXXXXX") - so this step is OPTIONAL, only for readable numbers.
#
# Usage:   ./provision_badge.sh <1..999> [/dev/ttyACM0]
# Example: ./provision_badge.sh 7            # -> SSID SYSRUPT-OT-V2-007, "BADGE 007"
#
# WARNING: this rewrites the badge's NVS partition, which CLEARS any saved CTF session/score. Run it on
# a fresh badge BEFORE handing it to a player. It does NOT change the app firmware.
set -euo pipefail

N="${1:?usage: provision_badge.sh <1..999> [port]}"
PORT="${2:-/dev/ttyACM0}"
IMG="espressif/idf:v6.1"
CHIP="esp32s3"
NVS_OFFSET="0x9000"   # nvs partition offset (partitions-16mb-no-sd.csv); confirm with gen_esp32part.py
NVS_SIZE="0x6000"     # 24K

if ! [ "$N" -ge 1 ] 2>/dev/null || ! [ "$N" -le 999 ] 2>/dev/null; then
  echo "ERROR: badge number must be an integer 1..999 (got '$N')"; exit 2
fi
NNN="$(printf '%03d' "$N")"
[ -e "$PORT" ] || { echo "ERROR: serial port $PORT not found (plug the badge in, or pass the port)"; exit 1; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
# NVS CSV: a namespace line, then the u16 key.
printf 'key,type,encoding,value\nvgbadge,namespace,,\nno,data,u16,%d\n' "$N" > "$WORK/badge.csv"

echo "Provisioning BADGE $NNN  ->  SSID SYSRUPT-OT-V2-$NNN   (port $PORT)"
echo "  (this clears the badge's NVS session/score - intended for a fresh badge)"

if command -v docker >/dev/null 2>&1 && docker image inspect "$IMG" >/dev/null 2>&1; then
  docker run --rm --device="$PORT" -v "$WORK":/w "$IMG" bash -lc "
    python /opt/esp/idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate /w/badge.csv /w/nvs.bin $NVS_SIZE &&
    python -m esptool --chip $CHIP -p $PORT write-flash $NVS_OFFSET /w/nvs.bin"
else
  command -v nvs_partition_gen.py >/dev/null 2>&1 || { echo "ERROR: need Docker image $IMG or ESP-IDF on PATH"; exit 1; }
  nvs_partition_gen.py generate "$WORK/badge.csv" "$WORK/nvs.bin" "$NVS_SIZE"
  esptool --chip "$CHIP" -p "$PORT" write-flash "$NVS_OFFSET" "$WORK/nvs.bin"
fi

echo "DONE. Reboot the badge; it will broadcast SSID SYSRUPT-OT-V2-$NNN and show BADGE $NNN."
