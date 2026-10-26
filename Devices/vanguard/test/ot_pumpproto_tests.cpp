// SPDX-License-Identifier: Apache-2.0
// Host tests for the custom pump protocol over the pump model (Challenge 3).
// Run: g++ -std=c++17 -I../source/ot ot_pumpproto_tests.cpp ../source/ot/net/PumpProto.cpp ../source/ot/sim/PumpSystem.cpp && ./a.out
#include "net/PumpProto.h"
#include "sim/PumpSystem.h"

#include <cstdio>

using namespace ot;
using namespace ot::net::pumpproto;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static void frame(uint8_t* f, uint16_t seq, uint8_t target, uint8_t action, bool goodCksum = true) {
    f[0] = SYNC0; f[1] = SYNC1; f[2] = (uint8_t)(seq >> 8); f[3] = (uint8_t)(seq & 0xFF);
    f[4] = target; f[5] = action; f[6] = 0;
    uint8_t s = 0; for (int i = 0; i < 7; i++) s += f[i];
    f[7] = goodCksum ? s : (uint8_t)(s + 1);
}

static uint8_t cmd(pump::PumpSystem& p, ConnState& cs, uint16_t seq, uint8_t tgt, uint8_t act, uint32_t now, bool good = true) {
    uint8_t in[8], out[8];
    frame(in, seq, tgt, act, good);
    size_t n = processFrame(p, now, in, 8, cs, out, sizeof(out));
    return (n >= 3) ? out[2] : 0xFF;
}

int main() {
    pump::PumpSystem p; p.reset(0);
    ConnState cs; // starts at BASELINE_SEQ

    // Replaying a captured frame (seq <= baseline) is rejected.
    CHECK(cmd(p, cs, 300, pump::TARGET_VALVE, pump::ACT_OPEN, 1000) == 1);
    CHECK(p.alerted());

    // Fresh crafted SELECT+OPEN above the baseline is accepted and opens the target.
    pump::PumpSystem p2; p2.reset(0);
    ConnState cs2;
    CHECK(cmd(p2, cs2, pump::BASELINE_SEQ + 1, pump::TARGET_VALVE, pump::ACT_SELECT, 2000) == 0);
    CHECK(cmd(p2, cs2, pump::BASELINE_SEQ + 2, pump::TARGET_VALVE, pump::ACT_OPEN, 2000) == 0);
    CHECK(p2.targetOpen());
    // Re-using a seq within the same connection is a replay.
    CHECK(cmd(p2, cs2, pump::BASELINE_SEQ + 2, pump::TARGET_VALVE, pump::ACT_OPEN, 2000) == 1);

    // Bad checksum -> status 3.
    CHECK(cmd(p2, cs2, pump::BASELINE_SEQ + 9, pump::TARGET_VALVE, pump::ACT_SELECT, 2000, false) == 3);

    // READFLAG before solve returns zero.
    uint8_t in[8], out[8];
    frame(in, 0, 0, ACT_READFLAG);
    size_t n = processFrame(p2, 2000, in, 8, cs2, out, sizeof(out));
    CHECK(n == 5 && out[3] == 0 && out[4] == 0);

    // PP rec 1: the status-read (READFLAG) request is the SAME 8-byte frame as a command, including the
    // padding/arg byte (in[6]) in the checksum - so the structural inference from the documented reply
    // format is guaranteed correct rather than a coincidence of implementation.
    {
        pump::PumpSystem p3; p3.reset(0);
        ConnState cs3;
        uint8_t fin[8], fout[8];
        // Corrupted checksum -> rejected exactly like a command (status 3), same frame shape.
        frame(fin, 0, 0, ACT_READFLAG, /*goodCksum=*/false);
        CHECK(processFrame(p3, 1000, fin, 8, cs3, fout, sizeof(fout)) == 3 && fout[2] == 3);
        // A non-zero padding/arg byte must be folded into the checksum for the frame to validate.
        frame(fin, 0, 2, ACT_READFLAG);                 // target=2 = flag word index
        fin[6] = 0x5A;                                  // padding/arg byte set
        { uint8_t s = 0; for (int i = 0; i < 7; i++) s += fin[i]; fin[7] = s; }
        size_t fn = processFrame(p3, 1000, fin, 8, cs3, fout, sizeof(fout));
        CHECK(fn == 5 && fout[0] == SYNC0 && fout[1] == SYNC1 && fout[2] == 0);
    }

    // PP rec 2: a fresh connection's anti-replay floor is the capture high-water mark, so "capture's
    // last sequence + 1" is the first accepted value (and a frame at the floor replays as stale).
    {
        pump::PumpSystem p4; p4.reset(0);
        ConnState cs4;
        CHECK(cmd(p4, cs4, pump::BASELINE_SEQ, pump::TARGET_VALVE, pump::ACT_SELECT, 1000) == 1);     // == floor -> replay
        CHECK(cmd(p4, cs4, pump::BASELINE_SEQ + 1, pump::TARGET_VALVE, pump::ACT_SELECT, 1000) == 0); // last+1 -> accepted
    }

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
