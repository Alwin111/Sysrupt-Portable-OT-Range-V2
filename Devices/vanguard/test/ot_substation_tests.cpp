// SPDX-License-Identifier: Apache-2.0
// Host-side tests for the IEC-104 substation state machine (Challenge 4). No FreeRTOS/lwIP.
// Run: g++ -std=c++17 -I../source/ot ot_substation_tests.cpp ../source/ot/sim/Substation.cpp && ./a.out
#include "sim/Substation.h"

#include <cstdio>
#include <string>

using namespace ot::sub;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* file, int line, const char* expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    g_fail++;
}
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static std::string readFlag(const Substation& s) {
    std::string out;
    for (uint16_t i = 0; i < FLAG_LEN_OBJS; i++) {
        const uint16_t w = s.flagWord(i);
        const char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
        if (hi) out += hi;
        if (lo) out += lo;
    }
    return out;
}

// Picks a timestamp that is NOT inside the interrogation-lock window of a substation reset at 0.
static uint32_t unlockedTime() { return INTERROG_WINDOW_MS + 500; }

static void testStartClosedLocked() {
    Substation s;
    s.reset(0);
    CHECK(!s.breakerOpen());
    CHECK(!s.solved());
    CHECK(readFlag(s).empty());
    // Just after reset the interrogation lock is asserted.
    s.tick(0);
    CHECK(s.interrogationLock());
    s.tick(unlockedTime());
    CHECK(!s.interrogationLock());
}

static void testDecoyIgnored() {
    Substation s;
    s.reset(0);
    const uint32_t t = unlockedTime();
    CHECK(s.command(IOA_TEST_BREAKER, 6, SUBSTATION_ORIG, true, t) == CmdResult::Ignored);
    CHECK(!s.breakerOpen()); // decoy never trips the real breaker
}

static void testDroppedDuringLock() {
    Substation s;
    s.reset(0);
    // During the lock window a correct command is silently dropped.
    CHECK(s.command(IOA_BREAKER, 6, SUBSTATION_ORIG, true, 100) == CmdResult::Dropped);
    CHECK(!s.breakerOpen());
}

static void testWrongOriginatorAlarms() {
    Substation s;
    s.reset(0);
    const uint32_t t = unlockedTime();
    CHECK(s.command(IOA_BREAKER, 6, 99, true, t) == CmdResult::Alarm);
    CHECK(!s.breakerOpen());
    CHECK(!s.solved());
}

static void testWrongCauseAlarms() {
    Substation s;
    s.reset(0);
    const uint32_t t = unlockedTime();
    CHECK(s.command(IOA_BREAKER, 8, SUBSTATION_ORIG, true, t) == CmdResult::Alarm); // not activation
    CHECK(!s.breakerOpen());
}

static void testCorrectTripSolves() {
    Substation s;
    s.reset(0);
    const uint32_t t = unlockedTime();
    // A single command with SCS=0 (OFF) is the breaker TRIP (open), per the player guide and the
    // IEC 60870-5 single-command convention. `on=false` => OFF => trip.
    CHECK(s.command(IOA_BREAKER, 6, SUBSTATION_ORIG, false, t) == CmdResult::Accepted);
    CHECK(s.breakerOpen());
    CHECK(s.lastTripOriginator() == SUBSTATION_ORIG);
    CHECK(s.solved());
    CHECK(readFlag(s) == std::string(IEC104_FLAG));
}

// Regression (BR-02): the breaker-trip polarity must match the player guide / IEC convention.
// An ON single command (SCS=1) is a breaker CLOSE, not a trip: it is accepted but must NOT open
// the breaker and must NOT solve. A player following the guide ("TRIP (OFF)") must succeed, and
// the inverted polarity must not.
static void testTripPolarity() {
    Substation s;
    s.reset(0);
    const uint32_t t = unlockedTime();
    CHECK(s.command(IOA_BREAKER, 6, SUBSTATION_ORIG, true, t) == CmdResult::Accepted); // ON = close
    CHECK(!s.breakerOpen());
    CHECK(!s.solved());
    CHECK(s.command(IOA_BREAKER, 6, SUBSTATION_ORIG, false, t + 10) == CmdResult::Accepted); // OFF = trip
    CHECK(s.breakerOpen());
    CHECK(s.solved());
}

// Regression: once the blackout is achieved the flag is latched; a later legitimate breaker CLOSE
// must not retract an already-captured flag.
static void testSolvedLatchesAfterClose() {
    Substation s;
    s.reset(0);
    const uint32_t t = unlockedTime();
    CHECK(s.command(IOA_BREAKER, 6, SUBSTATION_ORIG, false, t) == CmdResult::Accepted); // OFF = trip/open
    CHECK(s.solved());
    // A valid close (ON, same trusted originator) re-closes the breaker but must not un-solve.
    CHECK(s.command(IOA_BREAKER, 6, SUBSTATION_ORIG, true, t + 10) == CmdResult::Accepted); // ON = close
    CHECK(!s.breakerOpen());
    CHECK(s.solved());
    CHECK(readFlag(s) == std::string(IEC104_FLAG));
}

static void testEnumerationExposesLegitOriginator() {
    Substation s;
    s.reset(0);
    Point pts[64];
    const size_t n = s.enumerate(pts, 64);
    bool foundLegit = false, foundBreaker = false;
    for (size_t i = 0; i < n; i++) {
        if (pts[i].ioa == IOA_LAST_LEGIT_ORIG) { foundLegit = (pts[i].value == SUBSTATION_ORIG); }
        if (pts[i].ioa == IOA_BREAKER) { foundBreaker = true; }
    }
    CHECK(foundLegit);   // the address to use is discoverable by general interrogation
    CHECK(foundBreaker);
}

int main() {
    testStartClosedLocked();
    testDecoyIgnored();
    testDroppedDuringLock();
    testWrongOriginatorAlarms();
    testWrongCauseAlarms();
    testCorrectTripSolves();
    testTripPolarity();
    testSolvedLatchesAfterClose();
    testEnumerationExposesLegitOriginator();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
