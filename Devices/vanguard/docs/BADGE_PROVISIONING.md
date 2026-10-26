# Multi‑badge identity (unique Wi‑Fi SSID per badge)

Many identical Vanguard badges run in one room, so each badge has a **unique Wi‑Fi SSID** and shows a
**badge identity** on its starting ("JOIN THE WIFI TO START") screen, so a player can pick their own.

## How the identity is decided

One authoritative source: [`source/BadgeId.h`](../source/BadgeId.h) / `BadgeId.cpp`. Computed once at
boot, cached, never changes across screens / challenges / Wi‑Fi restarts. Used by the Wi‑Fi AP
(`OtService`), the starting screen (`ChallengeMenu`) and `OtService::ssid()` → the broadcast SSID, the
displayed SSID and `/api/state`'s `ssid` are always identical.

| Case | SSID | Starting‑screen label |
|---|---|---|
| **Provisioned** number `N` (1–999) in NVS | `SYSRUPT-OT-V2-NNN` (e.g. `-007`) | `BADGE 007` |
| **Not provisioned** (default) | `SYSRUPT-OT-V2-XXXXXX` (last 3 factory‑MAC bytes) | `BADGE ID XXXXXX` |

The default **MAC fallback already makes every badge unique with zero configuration** (the factory MAC
is unique + stable), and it is clearly labelled as a device id — not a fake sequential number. The AP
IP is **`http://10.0.1.1`** on every badge.

## Provisioning readable numbers 001–050 (optional)

Use [`tools/provision_badge.sh`](../tools/provision_badge.sh) on a **fresh** badge (it rewrites NVS,
clearing any saved session — do it before handing the badge to a player):

```bash
cd Devices/vanguard/tools
./provision_badge.sh 1     /dev/ttyACM0   # badge 001 -> SYSRUPT-OT-V2-001
./provision_badge.sh 2     /dev/ttyACM0   # badge 002 -> SYSRUPT-OT-V2-002
# ... up to 999; e.g.
./provision_badge.sh 50    /dev/ttyACM0   # badge 050 -> SYSRUPT-OT-V2-050
```

It writes `vgbadge/no` (u16) into the `nvs` partition (offset `0x9000`) via the ESP‑IDF
`nvs_partition_gen.py` + `esptool`, using the `espressif/idf:v6.1` Docker image. It changes **only**
NVS, not the app firmware — so you flash the same firmware to every badge and just run this once per
badge. Reboot the badge afterward.

**No per‑badge recompile is required.**

## What happens on bad / missing config

- No `vgbadge/no` key, or a value outside 1–999, or a corrupted/empty NVS → the firmware safely falls
  back to the MAC‑derived identity (`SYSRUPT-OT-V2-XXXXXX`, `BADGE ID XXXXXX`). No crash.
- The SSID is always ≤ 20 chars (well under the 32‑char Wi‑Fi limit). On the LCD a provisioned SSID
  (≤ 17 chars) shows in the big font; the 20‑char MAC fallback uses the compact font so it never
  clips at the 320 px display width.

## Verification

- **SSID broadcast:** `nmcli dev wifi list | grep SYSRUPT` → each badge shows a distinct SSID.
- **Consistency:** join a badge's AP, then `GET http://10.0.1.1/api/state` → its `ssid` field equals
  the SSID you joined (same source as the LCD label).
- **Stable:** reboot a badge → same SSID (MAC‑derived or provisioned number persists in NVS).
