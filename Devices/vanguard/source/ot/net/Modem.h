// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/DamSystem.h"

#include <cstddef>
#include <cstdint>

// Challenge 05 - Dial-In Dam modem emulator (over TCP 9003). Pure logic (host-tested); DamService
// wraps it with FreeRTOS + a TCP listener. The port answers with modem-style text, not HTTP.
//
// Handshake (PROVISIONAL tokens, discoverable from the banner):
//   connect  -> banner()                              (hints: send AT to begin)
//   "AT"     -> "OK"            (Idle  -> Ready)
//   "DIAL"   -> connect + menu  (Ready -> Menu)       ("ATD" is also accepted)
// Any out-of-order line in Idle/Ready drops the line ("NO CARRIER") and resets to Idle.
// Menu commands: STATUS | READ GATE | SET GATE <0-100>. Three bad commands drop the line
// ("LINE DROPPED") and reset to Idle (rate limit). The flag prints once the gate has been held at
// the target long enough (see DamSystem).
namespace ot::net::modem {

constexpr uint16_t PORT = 9003;

enum class State { Idle, Ready, Menu };

struct Session {
    State state = State::Idle;
    int invalid = 0;
};

// Writes the connect banner. Returns its length.
size_t banner(char* out, size_t cap);

// Processes one command line (without trailing CR/LF). Writes the modem response to `out`; returns
// its length. Updates the session and the dam state.
size_t process(Session& s, dam::DamSystem& d, const char* line, size_t len,
               uint32_t now_ms, char* out, size_t cap);

} // namespace ot::net::modem
