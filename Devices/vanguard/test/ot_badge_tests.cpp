// SPDX-License-Identifier: Apache-2.0
// Host test for the pure badge-identity formatter (source/BadgeId.h). The NVS read + factory-MAC read
// are firmware-only; this verifies the SSID/label formatting, the numbered-vs-fallback decision, the
// 32-char SSID limit and the uniqueness of the MAC fallback.
// Run: g++ -std=c++17 -I../source ot_badge_tests.cpp && ./a.out
#include "BadgeId.h"

#include <cstdio>
#include <cstring>

namespace {
int g_pass = 0, g_fail = 0;
void chk(bool c, const char* e) { if (c) g_pass++; else { fprintf(stderr, "FAIL: %s\n", e); g_fail++; } }
}
#define CHECK(e) chk((e), #e)

int main() {
    char ssid[33], label[24];
    const unsigned char mac[6] = {0xAC, 0x27, 0x6E, 0xC6, 0xF2, 0x30};

    // Numbered (1..999) -> zero-padded 3-digit "BADGE NNN".
    badge::format(7, mac, ssid, label);
    CHECK(std::strcmp(ssid, "SYSRUPT-OT-V2-007") == 0);
    CHECK(std::strcmp(label, "BADGE 007") == 0);
    badge::format(50, mac, ssid, label);
    CHECK(std::strcmp(ssid, "SYSRUPT-OT-V2-050") == 0);
    CHECK(std::strcmp(label, "BADGE 050") == 0);
    badge::format(999, mac, ssid, label);
    CHECK(std::strcmp(ssid, "SYSRUPT-OT-V2-999") == 0);

    // Unset / out-of-range -> MAC fallback (last byte, 2 hex chars), labelled as an ID.
    badge::format(-1, mac, ssid, label);
    CHECK(std::strcmp(ssid, "SYSRUPT-OT-V2-30") == 0);
    CHECK(std::strcmp(label, "BADGE ID 30") == 0);
    badge::format(0, mac, ssid, label);     // 0 is not a valid number
    CHECK(std::strcmp(ssid, "SYSRUPT-OT-V2-30") == 0);
    badge::format(1000, mac, ssid, label);  // > 999
    CHECK(std::strcmp(ssid, "SYSRUPT-OT-V2-30") == 0);

    // SSID always within the 32-char Wi-Fi limit; the fallback form is "SYSRUPT-OT-V2-XX" (16 chars).
    badge::format(123, mac, ssid, label); CHECK(std::strlen(ssid) <= 32);
    badge::format(-1, mac, ssid, label);  CHECK(std::strlen(ssid) <= 32);
    CHECK(std::strlen(ssid) == 16);       // "SYSRUPT-OT-V2-XX"

    // Two MACs with different last bytes -> different fallback SSIDs (best-effort uniqueness; a 2-char
    // suffix is only the last byte, so MACs sharing it collide by design - provision a number instead).
    const unsigned char mac2[6] = {0xAC, 0x27, 0x6E, 0x11, 0x22, 0x33};
    char ssidA[33], ssidB[33], lab[24];
    badge::format(-1, mac, ssidA, lab);
    badge::format(-1, mac2, ssidB, lab);
    CHECK(std::strcmp(ssidA, ssidB) != 0);

    printf("badge tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
