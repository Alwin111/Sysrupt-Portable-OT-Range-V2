// SPDX-License-Identifier: Apache-2.0
#include "PumpProto.h"

namespace ot::net::pumpproto {

size_t processFrame(pump::PumpSystem& p, uint32_t now_ms,
                    const uint8_t* in, size_t len, ConnState& cs, uint8_t* out, size_t outCap) {
    if (len != FRAME_LEN || in[0] != SYNC0 || in[1] != SYNC1 || outCap < 3) {
        return 0;
    }
    // Validate the checksum (sum of the first 7 bytes).
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) {
        sum = (uint8_t)(sum + in[i]);
    }
    const uint16_t seq = (uint16_t)((in[2] << 8) | in[3]);
    const uint8_t target = in[4];
    const uint8_t action = in[5];
    const uint8_t checksum = in[7];

    out[0] = SYNC0;
    out[1] = SYNC1;

    if (sum != checksum) {
        out[2] = 3; // bad
        return 3;
    }

    if (action == ACT_READFLAG) {
        const uint16_t val = p.flagWord(target); // target field carries the flag word index
        if (outCap < 5) return 0;
        out[2] = 0;
        out[3] = (uint8_t)(val >> 8);
        out[4] = (uint8_t)(val & 0xFF);
        return 5;
    }

    // Anti-replay: a command must carry a sequence number above everything seen on this connection
    // (which starts at the capture's high-water mark), so replaying a captured frame is rejected.
    if (seq <= cs.lastSeq) {
        p.replay(now_ms);
        out[2] = 1; // replay
        return 3;
    }
    cs.lastSeq = seq;

    const pump::CmdResult r = p.command(target, action, now_ms);
    switch (r) {
        case pump::CmdResult::Ok: out[2] = 0; break;
        case pump::CmdResult::Replay: out[2] = 1; break;
        case pump::CmdResult::OutOfOrder: out[2] = 2; break;
        case pump::CmdResult::Bad: out[2] = 3; break;
    }
    return 3;
}

} // namespace ot::net::pumpproto
