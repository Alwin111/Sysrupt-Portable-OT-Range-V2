// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/SafetySystem.h"

#include <cstddef>
#include <cstdint>

// A minimal "TriStation-lite" engineering protocol for the SIS challenge (Challenge 5), over TCP
// port 1502. Pure logic (host-tested); SisService wraps it with FreeRTOS + a TCP listener.
//
// Frame (all multi-byte big-endian):
//   request  : 0xAA 0x55 <cmd> <args...>
//   response : 0xAA 0x55 <cmd> <result/data...>
// Commands:
//   0x01 READ      args: reg(2)              -> resp: reg(2) value(2)
//   0x02 WRITE     args: reg(2) value(2)     -> resp: status(1)   (0 ok, 1 rejected, 2 read-only)
//   0x10 DOWNLOAD  args: fw(2) key(2) trip(1)-> resp: status(1)   (0 accepted,1 locked,2 bad-fw,3 bad-key)
namespace ot::net::tristation {

constexpr uint16_t PORT = 1502;
constexpr uint8_t SYNC0 = 0xAA;
constexpr uint8_t SYNC1 = 0x55;
constexpr uint8_t CMD_READ = 0x01;
constexpr uint8_t CMD_WRITE = 0x02;
constexpr uint8_t CMD_DOWNLOAD = 0x10;

// Total request length for a command (0 = unknown command / framing error).
size_t requestLen(uint8_t cmd);

// Processes one complete request frame; writes the response to `out`; returns its length (0 = none).
size_t processFrame(sis::SafetySystem& s, uint32_t now_ms,
                    const uint8_t* in, size_t len, uint8_t* out, size_t outCap);

} // namespace ot::net::tristation
