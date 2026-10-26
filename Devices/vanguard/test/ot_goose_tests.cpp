// SPDX-License-Identifier: Apache-2.0
// Host tests for the Second Blackout GOOSE-like sim + wire codec (Challenge 06).
// Run: g++ -std=c++17 -I../source/ot ot_goose_tests.cpp ../source/ot/sim/GooseSim.cpp ../source/ot/net/Goose.cpp && ./a.out
#include "net/Goose.h"
#include "sim/GooseSim.h"

#include <cstdio>
#include <cstring>

using namespace ot::goose;
namespace ng = ot::net::goose;

namespace { int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; } }
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static Pdu normal(uint32_t st, uint32_t sq) {
    return Pdu{ EXPECTED_APPID, st, sq, EXPECTED_CONFREV, /*breakerOpen*/false, /*test*/false };
}

int main() {
    // ----- wire codec round-trip + malformed rejection -----
    {
        uint8_t buf[ng::FRAME_LEN];
        Pdu p{ EXPECTED_APPID, 7, 3, EXPECTED_CONFREV, true, false };
        CHECK(ng::encode(p, buf, sizeof(buf)) == ng::FRAME_LEN);
        Pdu q{};
        CHECK(ng::parse(buf, ng::FRAME_LEN, q));
        CHECK(q.appid == p.appid && q.stNum == 7 && q.sqNum == 3 && q.confRev == p.confRev && q.breakerOpen && !q.test);
        CHECK(!ng::parse(buf, ng::FRAME_LEN - 1, q));   // wrong length
        uint8_t bad[ng::FRAME_LEN]; std::memcpy(bad, buf, sizeof(bad));
        bad[0] = 'X'; CHECK(!ng::parse(bad, ng::FRAME_LEN, q)); // bad magic
        uint8_t small[2] = { 'G', 'S' };
        CHECK(!ng::parse(small, sizeof(small), q));     // truncated
    }

    // ----- observe normal stream, then forge the next state change (happy path) -----
    {
        GooseSim s; s.reset();
        CHECK(s.process(normal(10, 0)) == Result::Accepted); // seed baseline (breaker closed)
        CHECK(!s.solved() && !s.breakerOpen());
        CHECK(s.process(normal(10, 1)) == Result::RejectedStale); // retransmit, same stNum
        CHECK(s.process(normal(10, 2)) == Result::RejectedStale);
        CHECK(s.lastStNum() == 10);
        // attacker forges breaker-open with the next stNum
        Pdu atk{ EXPECTED_APPID, 11, 0, EXPECTED_CONFREV, true, false };
        CHECK(s.process(atk) == Result::Accepted);
        CHECK(s.breakerOpen() && s.solved());
    }

    // ----- negatives: wrong APPID / confRev / test flag / stale all rejected, never solve -----
    {
        GooseSim s; s.reset();
        s.process(normal(5, 0)); // baseline
        Pdu wrongApp{ (uint16_t)(EXPECTED_APPID + 1), 6, 0, EXPECTED_CONFREV, true, false };
        CHECK(s.process(wrongApp) == Result::RejectedAppid);
        Pdu wrongRev{ EXPECTED_APPID, 6, 0, (uint16_t)(EXPECTED_CONFREV + 1), true, false };
        CHECK(s.process(wrongRev) == Result::RejectedConfRev);
        Pdu testFrame{ EXPECTED_APPID, 6, 0, EXPECTED_CONFREV, true, true };
        CHECK(s.process(testFrame) == Result::RejectedTest);
        Pdu stale{ EXPECTED_APPID, 5, 9, EXPECTED_CONFREV, true, false }; // stNum == last
        CHECK(s.process(stale) == Result::RejectedStale);
        CHECK(!s.solved() && !s.breakerOpen());          // none tripped the breaker
    }

    // ----- premature: just watching the normal (closed) stream never solves -----
    {
        GooseSim s; s.reset();
        for (uint32_t st = 1; st <= 5; st++) CHECK(s.process(normal(st, 0)) == Result::Accepted);
        CHECK(!s.solved() && !s.breakerOpen());
    }

    // ----- replay of an old captured open frame fails once the subscriber has moved on -----
    {
        GooseSim s; s.reset();
        Pdu open{ EXPECTED_APPID, 20, 0, EXPECTED_CONFREV, true, false };
        CHECK(s.process(open) == Result::Accepted && s.solved()); // first open solves
        GooseSim s2; s2.reset();
        s2.process(normal(21, 0));                       // subscriber already at stNum 21
        CHECK(s2.process(open) == Result::RejectedStale); // replaying stNum 20 is stale
        CHECK(!s2.solved() && !s2.breakerOpen());
    }

    // ----- reset + repeatability -----
    {
        GooseSim s; s.reset();
        s.process(normal(1, 0));
        Pdu open{ EXPECTED_APPID, 2, 0, EXPECTED_CONFREV, true, false };
        s.process(open); CHECK(s.solved());
        s.reset(); CHECK(!s.solved() && !s.breakerOpen() && s.lastStNum() == 0);
        s.process(normal(1, 0)); s.process(open);        // solvable again (stNum 2 > 1)
        CHECK(s.solved());
        CHECK(std::strcmp(s.flag(), GOOSE_FLAG) == 0);
    }

    printf("goose tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
