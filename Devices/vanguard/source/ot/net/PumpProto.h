// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/PumpSystem.h"

#include <cstddef>
#include <cstdint>

// The custom pump-control protocol for Challenge 3, over TCP port 7700. Pure logic (host-tested);
// PumpService wraps it with FreeRTOS + a TCP listener. A packet capture of this protocol is the
// companion/CTF asset players reverse.
//
// Fixed 8-byte frame (big-endian):
//   0x50 0x55 | seq(2) | target(1) | action(1) | arg(1) | checksum(1)
//   checksum = sum of the first 7 bytes, mod 256.
//   actions: 0x01 SELECT, 0x02 OPEN, 0x03 CLOSE, 0x05 READFLAG (target = flag word index)
// Response:
//   command  : 0x50 0x55 | status(1)            (0 ok, 1 replay, 2 out-of-order, 3 bad)
//   READFLAG : 0x50 0x55 | 0x00 | value(2)
namespace ot::net::pumpproto {

constexpr uint16_t PORT = 7700;
constexpr uint8_t SYNC0 = 0x50; // 'P'
constexpr uint8_t SYNC1 = 0x55; // 'U'
constexpr size_t FRAME_LEN = 8;
constexpr uint8_t ACT_READFLAG = 0x05;

// Per-connection sequence state. Each session starts at the capture's high-water mark, so replaying
// any captured frame is rejected, while every session advances its own sequence independently.
struct ConnState {
    uint16_t lastSeq = pump::BASELINE_SEQ;
};

// Processes one complete 8-byte frame; writes the response to `out`; returns its length (0 = none).
size_t processFrame(pump::PumpSystem& p, uint32_t now_ms,
                    const uint8_t* in, size_t len, ConnState& cs, uint8_t* out, size_t outCap);

} // namespace ot::net::pumpproto
