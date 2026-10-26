// SPDX-License-Identifier: Apache-2.0
// Host tests for the TriStation-lite protocol over the SIS model (Challenge 5).
// Run: g++ -std=c++17 -I../source/ot ot_tristation_tests.cpp ../source/ot/net/Tristation.cpp ../source/ot/sim/SafetySystem.cpp && ./a.out
#include "net/Tristation.h"
#include "sim/SafetySystem.h"

#include <cstdio>

using namespace ot;
using namespace ot::net::tristation;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static uint16_t doRead(sis::SafetySystem& s, uint16_t reg, uint32_t now) {
    uint8_t req[5] = { SYNC0, SYNC1, CMD_READ, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    uint8_t out[16];
    size_t n = processFrame(s, now, req, 5, out, sizeof(out));
    return (n == 7) ? (uint16_t)((out[5] << 8) | out[6]) : 0xDEAD;
}
static uint8_t doWrite(sis::SafetySystem& s, uint16_t reg, uint16_t val, uint32_t now) {
    uint8_t req[7] = { SYNC0, SYNC1, CMD_WRITE, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
                       (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    uint8_t out[8];
    size_t n = processFrame(s, now, req, 7, out, sizeof(out));
    return (n == 4) ? out[3] : 0xFF;
}
static uint8_t doDownload(sis::SafetySystem& s, uint16_t fw, uint16_t key, bool trip, uint32_t now) {
    uint8_t req[8] = { SYNC0, SYNC1, CMD_DOWNLOAD, (uint8_t)(fw >> 8), (uint8_t)(fw & 0xFF),
                       (uint8_t)(key >> 8), (uint8_t)(key & 0xFF), (uint8_t)(trip ? 1 : 0) };
    uint8_t out[8];
    size_t n = processFrame(s, now, req, 8, out, sizeof(out));
    return (n == 4) ? out[3] : 0xFF;
}

int main() {
    sis::SafetySystem s; s.reset(0);

    CHECK(doRead(s, sis::REG_FW_COMPAT, 0) == sis::FIRMWARE_COMPAT);
    CHECK(doRead(s, sis::REG_TRIP, 0) == 1);
    CHECK(doWrite(s, sis::REG_TRIP, 0, 1000) == 1);          // direct trip write rejected
    CHECK(doDownload(s, sis::FIRMWARE_COMPAT, 0, false, 1000) == 1); // locked

    const uint32_t t = 3000;
    CHECK(doWrite(s, sis::REG_LOCK, 1, t) == 0);             // open the download window
    const uint16_t key = doRead(s, sis::REG_DEBUG_KEY, t);   // fuzz out the rolling key
    CHECK(doDownload(s, 1234, key, false, t) == 2);          // bad firmware
    CHECK(doWrite(s, sis::REG_LOCK, 1, t) == 0);
    CHECK(doDownload(s, sis::FIRMWARE_COMPAT, key, false, t) == 0); // accepted
    CHECK(doRead(s, sis::REG_TRIP, t) == 0);                 // trip disabled
    CHECK(doRead(s, sis::REG_CHECKSUM_MATCH, t) == 1);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
