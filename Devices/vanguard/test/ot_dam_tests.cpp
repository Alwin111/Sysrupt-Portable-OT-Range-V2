// SPDX-License-Identifier: Apache-2.0
// Host tests for the Dial-In Dam modem protocol + gate model (Challenge 05).
// Run: g++ -std=c++17 -I../source/ot ot_dam_tests.cpp ../source/ot/net/Modem.cpp ../source/ot/sim/DamSystem.cpp && ./a.out
#include "net/Modem.h"
#include "sim/DamSystem.h"

#include <cstdio>
#include <cstring>

using namespace ot;
using namespace ot::net::modem;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

// Send one line, return the response as a null-terminated string in `out`.
static const char* line(Session& s, dam::DamSystem& d, const char* txt, uint32_t now, char* out, size_t cap) {
    size_t n = process(s, d, txt, std::strlen(txt), now, out, cap);
    out[n] = '\0';
    return out;
}
static bool has(const char* hay, const char* needle) { return std::strstr(hay, needle) != nullptr; }

int main() {
    char out[160];

    // --- banner + handshake order ---
    {
        Session s; dam::DamSystem d; d.reset(0);
        CHECK(banner(out, sizeof(out)) > 0 && has(out, "AT"));           // banner hints the handshake
        line(s, d, "STATUS", 0, out, sizeof(out));                       // command before handshake
        CHECK(has(out, "NO CARRIER") && s.state == State::Idle);         // out-of-order resets
        CHECK(has(line(s, d, "AT", 0, out, sizeof(out)), "OK"));
        CHECK(s.state == State::Ready);
        CHECK(has(line(s, d, "DIAL", 0, out, sizeof(out)), "MENU"));
        CHECK(s.state == State::Menu);
        line(s, d, "READ GATE", 0, out, sizeof(out));
        CHECK(has(out, "GATE=20"));                                      // INITIAL_GATE
    }

    // --- happy path: set to target, hold HOLD_MS -> solved, flag prints ---
    {
        Session s; dam::DamSystem d; d.reset(0);
        line(s, d, "AT", 0, out, sizeof(out));
        line(s, d, "DIAL", 0, out, sizeof(out));
        line(s, d, "SET GATE 85", 0, out, sizeof(out));                  // == TARGET_GATE
        CHECK(has(out, "GATE SET 85") && !d.solved());                   // not solved instantly
        d.tick(5000);  CHECK(!d.solved());                               // half the hold
        d.tick(10000); CHECK(d.solved());                                // full hold -> solved
        line(s, d, "STATUS", 10000, out, sizeof(out));
        CHECK(has(out, d.flag()));                                       // flag now readable
    }

    // --- negative: wrong gate never solves; moving off target resets the hold ---
    {
        Session s; dam::DamSystem d; d.reset(0);
        line(s, d, "AT", 0, out, sizeof(out)); line(s, d, "DIAL", 0, out, sizeof(out));
        line(s, d, "SET GATE 50", 0, out, sizeof(out));
        d.tick(20000); CHECK(!d.solved());                               // held wrong value -> no flag
        line(s, d, "SET GATE 85", 20000, out, sizeof(out));              // reach target at t=20s
        d.tick(25000);                                                   // only 5s held
        line(s, d, "SET GATE 84", 25000, out, sizeof(out));              // move off target -> reset
        d.tick(35000); CHECK(!d.solved());
    }

    // --- range + malformed handling ---
    {
        Session s; dam::DamSystem d; d.reset(0);
        line(s, d, "AT", 0, out, sizeof(out)); line(s, d, "DIAL", 0, out, sizeof(out));
        CHECK(has(line(s, d, "SET GATE 999", 0, out, sizeof(out)), "ERR"));
        CHECK(has(line(s, d, "SET GATE abc", 0, out, sizeof(out)), "ERR"));
        CHECK(d.gate() == 20);                                           // unchanged by bad input
    }

    // --- rate limit: three bad menu commands drop the line and reset ---
    {
        Session s; dam::DamSystem d; d.reset(0);
        line(s, d, "AT", 0, out, sizeof(out)); line(s, d, "DIAL", 0, out, sizeof(out));
        CHECK(has(line(s, d, "FOO", 0, out, sizeof(out)), "?"));
        CHECK(has(line(s, d, "BAR", 0, out, sizeof(out)), "?"));
        CHECK(has(line(s, d, "BAZ", 0, out, sizeof(out)), "LINE DROPPED"));
        CHECK(s.state == State::Idle);                                   // session reset
    }

    // --- case-insensitive + reset repeatability ---
    {
        Session s; dam::DamSystem d; d.reset(0);
        line(s, d, "at", 0, out, sizeof(out)); line(s, d, "dial", 0, out, sizeof(out));
        line(s, d, "set gate 85", 0, out, sizeof(out)); d.tick(10000);
        CHECK(d.solved());
        d.reset(100000);                                                 // reset clears everything
        CHECK(!d.solved() && d.gate() == 20);
        Session s2; line(s2, d, "AT", 100000, out, sizeof(out));
        line(s2, d, "DIAL", 100000, out, sizeof(out));
        line(s2, d, "SET GATE 85", 100000, out, sizeof(out)); d.tick(110000);
        CHECK(d.solved());                                               // solvable again after reset
    }

    printf("dam tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
