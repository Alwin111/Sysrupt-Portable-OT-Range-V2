# Hardware

The SYSRUPT "Vanguard" badge is a custom **ESP32-S3** board running Tactility OS. The authoritative
description is the devicetree
[`bi0s,vanguard.dts`](../../Devices/vanguard/bi0s,vanguard.dts) and the device descriptor
[`device.properties`](../../Devices/vanguard/device.properties); the facts below come from them.

> This is **not** a schematic or PCB package. This repository contains no Gerber/CAD files, and several
> on-board peripherals (LoRa radio, LEDs, buzzer, battery sensing) are **not** driven by the firmware.

## SoC and memory

| Property | Value |
|----------|-------|
| MCU | ESP32-S3-WROOM-1 |
| Flash | 16 MB, mode **DIO** |
| PSRAM | **None** |
| Bluetooth | Disabled |
| User storage | Internal only (no SD slot); progress in NVS |
| App partition | 6 MB (`0x600000`); firmware ≈ 4 MB, ~35% free |

**No PSRAM is the dominant constraint.** The UI and network handlers run in a tight internal-heap
budget (idle free heap ≈ 15–35 KB, dipping toward ~8 KB under load), which is why per-challenge text and
web-handler buffers are kept small — and why players must be gentle with connections.

## Display

| Property | Value |
|----------|-------|
| Controller | Sitronix ST7789 |
| Resolution | 320 × 240, landscape |
| Bus | FSPI (`SPI2_HOST`), 40 MHz |
| Colour | 16-bit RGB565 |
| Backlight | Hardwired on (no GPIO control) |

Display pins (GPIO): CS 10, MOSI 11, SCLK 12, MISO 13, DC 5, RESET 4. The FSPI bus is shared with an
SX1261 LoRa radio and two 74HC595 LED shift registers; the firmware keeps those deselected/latched so
display traffic never reaches them.

## Input

Ten active-low inputs: a **5-way joystick** (up/down/left/right/centre) for navigation plus **S1–S5**
system buttons. Navigation keys go to LVGL; system keys are delivered only to the device module, so they
behave the same in every app.

## Programming

USB-C serial at 115200 (`/dev/ttyACM0` on Linux). Flash with
[`flash_badge.sh`](../../flash_badge.sh); build with ESP-IDF v6.1 (see [INSTALL.md](../../INSTALL.md)).

## Full detail

The deep hardware reference (pin maps, shared-bus handling, power) is
[`Devices/vanguard/docs/HARDWARE.md`](../../Devices/vanguard/docs/HARDWARE.md).
