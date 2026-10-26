// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/Substation.h"

#include <cstddef>
#include <cstdint>

// A minimal IEC 60870-5-104 (IEC-104) server over the substation state (Challenge 4). Pure logic
// (no sockets), so it is host-unit-tested; Iec104Service wraps it with FreeRTOS + a TCP listener on
// port 2404.
//
// Supported: APCI framing (U-frames STARTDT/TESTFR, S-frames, I-frames with send/receive sequence
// numbers) and the ASDUs the challenge needs - C_IC (general interrogation, type 100), C_SC (single
// command, type 45), M_SP_NA (type 1) and M_ME_NB (type 11) in interrogation/spontaneous responses.
namespace ot::net::iec104 {

constexpr uint16_t PORT = 2404;
constexpr size_t MAX_APDU = 255; // 0x68 + len byte caps the rest at 253

// Per-connection sequence state.
struct ConnState {
    uint16_t ns = 0; // our send sequence number
    uint16_t nr = 0; // our receive sequence number (count of I-frames received)
};

// Processes ONE complete APDU (apdu[0]==0x68, apdu[1]==len). Appends any response APDUs to `out`
// and returns the number of bytes written (0 if none). `now_ms` drives the interrogation lock.
size_t processApdu(sub::Substation& s, uint32_t now_ms,
                   const uint8_t* apdu, size_t len,
                   ConnState& cs, uint8_t* out, size_t outCap);

} // namespace ot::net::iec104
