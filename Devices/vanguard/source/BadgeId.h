// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdio>

// The single authoritative per-badge identity. Many identical Vanguard badges run in one room, so
// each gets a UNIQUE Wi-Fi SSID and a human-visible label. The identity is computed ONCE (cached), so
// it never changes across screens, challenges or Wi-Fi restarts, and it is used by BOTH the Wi-Fi AP
// (OtService) and the starting LCD screen (ChallengeMenu) so the broadcast SSID and the displayed
// SSID are always identical.
//
//   * If a badge NUMBER (1..999) has been provisioned in NVS (namespace "vgbadge", key "no"), the
//     SSID is "SYSRUPT-OT-V2-NNN" and the label is "BADGE NNN".
//   * Otherwise a STABLE fallback is derived from the factory MAC: SSID "SYSRUPT-OT-V2-XX" and label
//     "BADGE ID XX" (XX = the last MAC byte in hex, 2 chars, e.g. A5/B8/30). Short by request; it is
//     stable across reboots. (2 hex chars = 256 values, so distinct badges in one room may rarely
//     share a suffix - provision a number via NVS if a guaranteed-unique id is needed.)
namespace badge {

// Pure formatter (no hardware deps), so it is host-testable. A valid number (1..999) produces the
// numbered identity; anything else produces the MAC-derived fallback. Buffers must be >= the sizes
// shown (ssid 33, label 24); the longest SSID "SYSRUPT-OT-V2-NNN" is 17 chars, well under 32.
inline void format(int num, const unsigned char mac[6], char ssid[33], char label[24]) {
    if (num >= 1 && num <= 999) {
        std::snprintf(ssid, 33, "SYSRUPT-OT-V2-%03d", num);
        std::snprintf(label, 24, "BADGE %03d", num);
    } else {
        std::snprintf(ssid, 33, "SYSRUPT-OT-V2-%02X", mac[5]);   // 2 hex chars (last MAC byte)
        std::snprintf(label, 24, "BADGE ID %02X", mac[5]);
    }
}

const char* ssid();   // full SSID, e.g. "SYSRUPT-OT-V2-007" (<= 32 chars). Same string the AP broadcasts.
const char* label();  // display label, e.g. "BADGE 007" or "BADGE ID A1B2C3".
bool configured();    // true iff a 1..999 number was provisioned in NVS.
int number();         // the provisioned number (1..999), or -1 when using the MAC fallback.

} // namespace badge
