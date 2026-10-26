// SPDX-License-Identifier: Apache-2.0
// Host tests for the Safety-System Integrity model (Challenge 5).
// Run: g++ -std=c++17 -I../source/ot ot_sis_tests.cpp ../source/ot/sim/SafetySystem.cpp && ./a.out
#include "sim/SafetySystem.h"

#include <cstdio>
#include <string>

using namespace ot::sis;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static std::string readFlag(const SafetySystem& s, uint32_t now) {
    std::string out;
    for (uint16_t i = 0; i < FLAG_LEN_REGS; i++) {
        const uint16_t w = s.read(REG_FLAG_BASE + i, now);
        const char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
        if (hi) out += hi;
        if (lo) out += lo;
    }
    return out;
}

static void testInitialState() {
    SafetySystem s; s.reset(0);
    CHECK(s.tripEnabled());
    CHECK(s.checksumMatch());
    CHECK(!s.solved());
    CHECK(s.read(REG_FW_COMPAT, 0) == FIRMWARE_COMPAT);
}

static void testDirectTripWriteRejected() {
    SafetySystem s; s.reset(0);
    CHECK(s.write(REG_TRIP, 0, 1000) == WriteResult::Rejected);
    CHECK(s.tripEnabled()); // unchanged
}

static void testDownloadNeedsLock() {
    SafetySystem s; s.reset(0);
    CHECK(s.programDownload(FIRMWARE_COMPAT, s.currentKey(1000), false, 1000) == DownloadResult::Locked);
}

static void testDownloadNeedsFirmwareAndKey() {
    SafetySystem s; s.reset(0);
    const uint32_t t = 3000;
    s.write(REG_LOCK, 1, t); // open window
    CHECK(s.programDownload(1234, s.currentKey(t), false, t) == DownloadResult::BadFirmware);
    s.write(REG_LOCK, 1, t);
    CHECK(s.programDownload(FIRMWARE_COMPAT, 0x0000, false, t) == DownloadResult::BadKey);
    CHECK(s.tripEnabled()); // still armed
}

static void testValidDownloadDisablesTripAndSolves() {
    SafetySystem s; s.reset(0);
    const uint32_t t = 3000;
    s.write(REG_LOCK, 1, t);                              // open the download window
    const uint16_t key = s.read(REG_DEBUG_KEY, t);        // read the rolling key (fuzz)
    CHECK(s.programDownload(FIRMWARE_COMPAT, key, false, t) == DownloadResult::Accepted);
    CHECK(!s.tripEnabled());
    CHECK(s.checksumMatch());
    CHECK(readFlag(s, t).empty()); // not yet - needs the hold

    // Hold the disabled-trip + valid-checksum state for CHECKSUM_HOLD_MS.
    s.tick(t + 100);
    s.tick(t + 100 + CHECKSUM_HOLD_MS + 100);
    CHECK(s.solved());
    CHECK(readFlag(s, t + 6000) == std::string(SIS_FLAG));
}

static void testLockReLocks() {
    SafetySystem s; s.reset(0);
    s.write(REG_LOCK, 1, 1000);
    CHECK(s.lockOpen());
    s.tick(1000 + LOCK_WINDOW_MS + 1);
    CHECK(!s.lockOpen()); // auto re-locked
}

static void testKeyRolls() {
    SafetySystem s; s.reset(0);
    CHECK(s.currentKey(0) != s.currentKey(KEY_ROLL_MS + 1));
}

static void testDecoyRegisters() {
    // Decoy diagnostics near the key change on their own cadence, are never equal to the key, and are
    // rejected (BadKey) if submitted to a download - so a scan returns several candidates and the first
    // changing register found is not guaranteed correct.
    SafetySystem s; s.reset(0);
    CHECK(s.read(REG_DECOY_UPTIME, 0) != s.read(REG_DECOY_UPTIME, 1500)); // ~1 s cadence
    CHECK(s.read(REG_DECOY_DIAG, 0) != s.read(REG_DECOY_DIAG, 4000));     // ~2 s cadence, different formula
    const uint32_t t = 3000;
    const uint16_t decoyA = s.read(REG_DECOY_UPTIME, t);
    const uint16_t decoyB = s.read(REG_DECOY_DIAG, t);
    CHECK(decoyA != s.currentKey(t) && decoyB != s.currentKey(t));
    s.write(REG_LOCK, 1, t);
    CHECK(s.programDownload(FIRMWARE_COMPAT, decoyA, false, t) == DownloadResult::BadKey);
    s.write(REG_LOCK, 1, t);
    CHECK(s.programDownload(FIRMWARE_COMPAT, decoyB, false, t) == DownloadResult::BadKey);
    // The real key still works.
    s.write(REG_LOCK, 1, t);
    CHECK(s.programDownload(FIRMWARE_COMPAT, s.read(REG_DEBUG_KEY, t), false, t) == DownloadResult::Accepted);
}

int main() {
    testInitialState();
    testDirectTripWriteRejected();
    testDownloadNeedsLock();
    testDownloadNeedsFirmwareAndKey();
    testValidDownloadDisablesTripAndSolves();
    testLockReLocks();
    testKeyRolls();
    testDecoyRegisters();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
