// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 05 - Dial-In Dam. Pure logic (host-tested); the modem protocol (net/Modem) is a thin
// view onto this state and DamService wraps it with FreeRTOS + a TCP listener.
//
// Scenario: a small dam exposes a legacy dial-in modem interface (simulated over TCP 9003). The
// gate can be driven from the modem's command menu. There is no legitimate operator session behind
// the modem path, so moving the gate to the target and holding it demonstrates the weakness.
// Solve = sluice-gate position == TARGET_GATE continuously for HOLD_MS; the flag then becomes
// readable from the modem session. Changing the gate away from the target resets the hold timer.
//
// NOTE: the exact gate target, initial position and the modem handshake tokens are not fixed by the
// source spec. The values below are PROVISIONAL, chosen for deterministic testing.
namespace ot::dam {

constexpr uint8_t TARGET_GATE = 85;   // PROVISIONAL: the seeded "unsafe" gate position to reach
constexpr uint8_t INITIAL_GATE = 20;  // PROVISIONAL: baseline gate position after reset
constexpr uint32_t HOLD_MS = 10000;   // gate must stay at TARGET_GATE this long

constexpr const char* DAM_FLAG = "flag{d1al_in_dam_g4te_contr0l}";
constexpr size_t dam_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(dam_cstrlen(DAM_FLAG) <= 32, "DAM_FLAG must fit 32 bytes");

class DamSystem {
public:
    void reset(uint32_t now_ms);
    void tick(uint32_t now_ms);

    // Set the gate position (0-100). Starts/continues the hold timer at the target, resets it off it.
    void setGate(uint8_t pos, uint32_t now_ms);

    uint8_t gate() const { return gate_; }
    bool atTarget() const { return gate_ == TARGET_GATE; }
    bool solved() const { return solved_; }
    const char* flag() const { return DAM_FLAG; }

private:
    uint8_t gate_ = INITIAL_GATE;
    bool hold_running_ = false;
    uint32_t hold_since_ms_ = 0;
    bool solved_ = false;
};

} // namespace ot::dam
