// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/Historian.h"

#include <cstddef>

// Text query protocol for Challenge 6 (Historian Forensics), over TCP port 9000. Line-based ASCII so
// it reverses trivially (the challenge is the forensics, not the wire format). Pure logic; the
// HistorianService wraps it with FreeRTOS + a TCP listener.
//
// Commands (one per line, case-insensitive):
//   HELP              list commands
//   TAGS              list tag names
//   READ <tag>        clean sample history (purged rows hidden)
//   RAW  <tag>        raw history incl. soft-deleted rows  <-- recovers the flag on the tampered tag
//   AUDIT             audit trail (names the purged tag/seq)
namespace ot::net::histproto {

// Processes one command line (without trailing newline). Writes an ASCII reply to `out` and returns
// its length (always < outCap; NUL-terminated within outCap).
size_t processLine(hist::Historian& h, const char* line, size_t len, char* out, size_t outCap);

} // namespace ot::net::histproto
