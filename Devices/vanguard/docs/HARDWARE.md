# Hardware guide

The SYSRUPT "Vanguard" badge is a custom ESP32‑S3 board. The authoritative hardware description is the
devicetree [`bi0s,vanguard.dts`](../bi0s,vanguard.dts) and the device descriptor
[`device.properties`](../device.properties); the facts below are taken from them. Where the devicetree
notes something is **not configured yet**, this document says so rather than implying support.

> This guide documents the board as the firmware configures it. It is **not** a schematic or a PCB
> package, and no Gerber/CAD files are included in this repository.

## SoC and memory

| Property | Value | Source |
|---|---|---|
| MCU | ESP32‑S3‑WROOM‑1 | `device.properties` / DTS |
| Flash | 16 MB, flash mode **DIO** | `device.properties` |
| PSRAM | **None** (eFuse `PSRAM_CAP=0`) | `device.properties` |
| Bluetooth | Disabled | `device.properties` |
| User storage | **Internal only** (no SD slot) | `device.properties` |
| App partition | `0x600000` (6 MB); firmware ≈ `0x3266b0`, ~47% free | build output |

The lack of PSRAM is the dominant design constraint: the UI and the network handlers run in a tight
internal‑heap budget (a challenge view leaves only a few KB free), which is why per‑challenge text and
web‑handler buffers are kept small.

## Display

| Property | Value |
|---|---|
| Controller | Sitronix **ST7789** |
| Resolution | **320 × 240**, landscape |
| Orientation | `swap-xy` + `mirror-x` + `invert-color` (equivalent to the original firmware's `setRotation(3)`) |
| Bus | FSPI (`SPI2_HOST`), 40 MHz pixel clock |
| Colour depth | 16‑bit (RGB565) |
| Backlight | Hardwired on (no GPIO control) |

**Display pins (GPIO):** CS 10, MOSI 11, SCLK 12, MISO 13, DC 5, RESET 4.

The FSPI bus is **shared**: the ST7789 (CS 10), an SX1261 LoRa radio (NSS 9) and two 74HC595 LED shift
registers sit on the same MOSI/SCLK. The LoRa NSS is claimed by the SPI controller so it stays
deselected (a floating NSS would make the radio misread display traffic as commands). The LED shift
registers have no chip‑select — their latch (GPIO 8) is **hogged LOW** so display traffic never reaches
the LED outputs.

## Input

Ten active‑low inputs (verified on hardware). Navigation keys are delivered to LVGL; system keys are
delivered only to the device module (`emit-only`), so they behave the same in every app and cannot type
characters.

| Function | GPIO | Key code | Role |
|---|---|---|---|
| Joystick UP | 45 | `ARROW_UP` | nav |
| Joystick DOWN | 37 | `ARROW_DOWN` | nav |
| Joystick LEFT | 47 | `ARROW_LEFT` | nav |
| Joystick RIGHT | 48 | `ARROW_RIGHT` | nav |
| Joystick SELECT | 21 | `ENTER` | nav |
| S5 OK | 18 | `ENTER` | nav |
| S4 BACK | 38 | `ESCAPE` | system |
| S2 DISP | 17 | private‑use | system |
| S1 PAUSE | 15 | private‑use | system (detected, **unassigned**) |
| S3 START | 16 | private‑use | system (detected, **unassigned**) |

All inputs idle HIGH through pull‑ups and read LOW when pressed; the internal pull‑up is enabled as a
fallback.

## Serial / debug

The badge is programmed and monitored over USB (USB‑C). On Linux it typically enumerates as
`/dev/ttyACM0`. Use the ESP‑IDF monitor or any serial terminal at the IDF default (115200 baud) for
boot logs. The `flash_badge.sh` helper wraps flashing (see [FIRMWARE_DEVELOPMENT.md](FIRMWARE_DEVELOPMENT.md)).

## Networking

The firmware enables IPv4 forwarding and NAPT (`CONFIG_LWIP_IP_FORWARD`, `CONFIG_LWIP_IPV4_NAPT`,
`..._PORTMAP`) and raises the socket pool to 16 (`CONFIG_LWIP_MAX_SOCKETS=16`) to support the
concurrent protocol listeners. The badge is the Wi‑Fi AP and gateway at `10.0.1.1`.

## Not configured (present on the board, unused by the firmware)

The devicetree explicitly lists hardware the firmware does **not** drive today. Do not assume these
work:

- **LoRa SX1261** radio (NSS 9, DIO1 6, BUSY 3) — SPI line claimed/parked only.
- **LED shift registers** (2× 74HC595) — latch hogged low; LEDs are not driven.
- **Buzzer** (GPIO 7), **status LED** (GPIO 2), **battery sense** (GPIO 1).

## Known limitations

- No PSRAM → tight internal‑heap budget; the UI is tuned for it.
- Backlight is not software‑controllable (hardwired on).
- The LoRa radio, LEDs, buzzer and battery sensing are not implemented in firmware.
- `device.properties` marks the device `incubating=true`; the panel diagonal (2.4″) is assumed and
  only affects UI scaling.
