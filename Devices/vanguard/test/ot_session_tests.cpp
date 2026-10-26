// SPDX-License-Identifier: Apache-2.0
// Host tests for the persisted-session serialization (SessionCodec) used to survive reboots.
// Run: g++ -std=c++17 -I../source/ui ot_session_tests.cpp ../source/ui/SessionCodec.cpp && ./a.out
#include "SessionCodec.h"

#include <cstdio>
#include <cstring>

using namespace ot::ui;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static bool sameState(const SessionState& a, const SessionState& b) {
    if (a.completedMask != b.completedMask || a.revealCount != b.revealCount) return false;
    for (int i = 0; i < a.revealCount; i++)
        if (a.reveals[i].num != b.reveals[i].num || a.reveals[i].idx != b.reveals[i].idx) return false;
    return true;
}

int main() {
    char buf[512];

    // --- empty/default state roundtrips ---
    {
        SessionState s;
        CHECK(sessionSerialize(s, buf, sizeof(buf)) > 0);
        SessionState out;
        CHECK(sessionDeserialize(buf, out));
        CHECK(sameState(s, out));
        CHECK(out.completedMask == 0 && out.revealCount == 0);
    }

    // --- typical state (some completions + hint reveals) roundtrips exactly ---
    {
        SessionState s;
        s.completedMask = (1u << 5) | (1u << 1) | (1u << 12); // challenges 1, 5, 12
        s.revealCount = 3;
        s.reveals[0] = { 5, 0 };
        s.reveals[1] = { 5, 1 };
        s.reveals[2] = { 10, 2 };
        CHECK(sessionSerialize(s, buf, sizeof(buf)) > 0);
        SessionState out;
        CHECK(sessionDeserialize(buf, out));
        CHECK(sameState(s, out));
        CHECK(out.completedMask == s.completedMask);
    }

    // --- full reveal array (SESSION_MAX_REVEALS) roundtrips ---
    {
        SessionState s;
        s.completedMask = 0xFFFFFFFFu;
        s.revealCount = SESSION_MAX_REVEALS;
        for (int i = 0; i < SESSION_MAX_REVEALS; i++) s.reveals[i] = { i % 13, i % 4 };
        CHECK(sessionSerialize(s, buf, sizeof(buf)) > 0);
        SessionState out;
        CHECK(sessionDeserialize(buf, out));
        CHECK(sameState(s, out));
    }

    // --- invalid inputs are rejected and leave defaults ---
    {
        SessionState out;
        out.completedMask = 0xDEAD; out.revealCount = 7; // pre-dirty to prove it gets reset
        CHECK(!sessionDeserialize(nullptr, out));
        CHECK(out.completedMask == 0 && out.revealCount == 0);      // reset to defaults
        CHECK(!sessionDeserialize("", out));
        CHECK(!sessionDeserialize("garbage", out));
        CHECK(!sessionDeserialize("2 5 0", out));                   // wrong version
        CHECK(!sessionDeserialize("1 5 99", out));                  // count out of range
        CHECK(!sessionDeserialize("1 5 1 40 0", out));              // reveal num out of range
        CHECK(!sessionDeserialize("1 5 2 1 0", out));               // count says 2 but only 1 pair
    }

    // --- a valid payload after a prior failed parse still works (no sticky state) ---
    {
        SessionState out;
        CHECK(!sessionDeserialize("1 5 99", out));
        CHECK(sessionDeserialize("1 6 1 3 2", out));
        CHECK(out.completedMask == 6 && out.revealCount == 1 && out.reveals[0].num == 3 && out.reveals[0].idx == 2);
    }

    // --- serialize into too-small buffer fails cleanly ---
    {
        SessionState s;
        s.completedMask = 123456;
        char tiny[4];
        CHECK(sessionSerialize(s, tiny, sizeof(tiny)) == -1);
        CHECK(sessionSerialize(s, buf, 0) == -1);
    }

    printf("session tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
