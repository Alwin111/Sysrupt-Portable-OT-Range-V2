// SPDX-License-Identifier: Apache-2.0
// Host tests for the Custom Pump Protocol model (Challenge 3).
// Run: g++ -std=c++17 -I../source/ot ot_pump_tests.cpp ../source/ot/sim/PumpSystem.cpp && ./a.out
#include "sim/PumpSystem.h"

#include <cstdio>
#include <string>

using namespace ot::pump;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static std::string readFlag(const PumpSystem& p) {
    std::string out;
    for (uint16_t i = 0; i < FLAG_LEN; i++) {
        const uint16_t w = p.flagWord(i);
        const char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
        if (hi) out += hi;
        if (lo) out += lo;
    }
    return out;
}

static void testReplayAlerts() {
    PumpSystem p; p.reset(0);
    p.replay(1000); // a detected replay raises an alert and shuts the valve
    CHECK(p.alerted());
    CHECK(!p.targetOpen());
}

static void testOpenWithoutSelectAlerts() {
    PumpSystem p; p.reset(0);
    CHECK(p.command(TARGET_VALVE, ACT_OPEN, 1000) == CmdResult::OutOfOrder);
    CHECK(p.alerted());
}

static void testDecoyDoesNotOpenTarget() {
    PumpSystem p; p.reset(0);
    CHECK(p.command(DECOY_IDS[0], ACT_SELECT, 1000) == CmdResult::Ok);
    CHECK(p.command(DECOY_IDS[0], ACT_OPEN, 1000) == CmdResult::Ok);
    CHECK(!p.targetOpen()); // a decoy valve is irrelevant
}

static void testCraftedSequenceSolves() {
    PumpSystem p; p.reset(0);
    const uint32_t t = 1000;
    CHECK(p.command(TARGET_VALVE, ACT_SELECT, t) == CmdResult::Ok);
    CHECK(p.command(TARGET_VALVE, ACT_OPEN, t) == CmdResult::Ok);
    CHECK(p.targetOpen());
    CHECK(!p.alerted());
    CHECK(readFlag(p).empty()); // needs the hold

    p.tick(t + 100);
    p.tick(t + 100 + HOLD_MS + 100);
    CHECK(p.solved());
    CHECK(readFlag(p) == std::string(PUMP_FLAG));
}

static void testAlertBlocksHold() {
    PumpSystem p; p.reset(0);
    uint32_t t = 1000;
    // Trigger an alert, then immediately do a valid open: the lockout must delay the solve.
    p.replay(t); // alert, lockout until t+ALERT_LOCKOUT_MS
    CHECK(p.alerted());
    p.command(TARGET_VALVE, ACT_SELECT, t);
    p.command(TARGET_VALVE, ACT_OPEN, t);
    CHECK(p.targetOpen());
    p.tick(t + 5000);
    CHECK(!p.solved());
    for (uint32_t e = 0; e <= ALERT_LOCKOUT_MS + HOLD_MS + 500; e += 200) {
        p.tick(t + e);
    }
    CHECK(p.solved());
}

int main() {
    testReplayAlerts();
    testOpenWithoutSelectAlerts();
    testDecoyDoesNotOpenTarget();
    testCraftedSequenceSolves();
    testAlertBlocksHold();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
