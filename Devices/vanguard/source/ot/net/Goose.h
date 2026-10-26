// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/GooseSim.h"

#include <cstddef>
#include <cstdint>

// Wire codec for the Challenge 06 training GOOSE frame (host-tested). Fixed 16-byte big-endian
// layout carried in one UDP datagram - a deterministic stand-in for L2 GOOSE (see sim/GooseSim.h for
// why L2 is not used). The APPID and confRev are observable in the repeating broadcast stream and are
// documented (with the breaker-open dataset bit) in the /evidence "goose_monitor" note, so a player
// can forge a matching frame; there is no separate human-readable banner.
//
//   off 0  : magic 'G','S'            (0x47 0x53)
//   off 2  : version                  (0x01)
//   off 3  : flags                    bit0 = breakerOpen, bit1 = test
//   off 4  : appid                    (u16 BE)
//   off 6  : stNum                    (u32 BE)
//   off 10 : sqNum                    (u32 BE)
//   off 14 : confRev                  (u16 BE)
namespace ot::net::goose {

constexpr size_t FRAME_LEN = 16;

// Encodes a PDU into exactly FRAME_LEN bytes. `buf` must hold >= FRAME_LEN. Returns bytes written.
size_t encode(const ot::goose::Pdu& p, uint8_t* buf, size_t cap);

// Parses a datagram into a PDU. Returns true on a well-formed frame (correct magic/version/length),
// false otherwise (malformed frames are dropped before they reach the subscriber logic).
bool parse(const uint8_t* buf, size_t len, ot::goose::Pdu& out);

} // namespace ot::net::goose
