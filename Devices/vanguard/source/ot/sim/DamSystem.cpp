// SPDX-License-Identifier: Apache-2.0
#include "DamSystem.h"

namespace ot::dam {

void DamSystem::reset(uint32_t now_ms) {
    gate_ = INITIAL_GATE;
    hold_running_ = false;
    hold_since_ms_ = now_ms;
    solved_ = false;
}

void DamSystem::setGate(uint8_t pos, uint32_t now_ms) {
    if (pos > 100) pos = 100;
    gate_ = pos;
    if (gate_ == TARGET_GATE) {
        if (!hold_running_) { hold_running_ = true; hold_since_ms_ = now_ms; }
    } else {
        hold_running_ = false; // moved off target -> hold timer resets
    }
}

void DamSystem::tick(uint32_t now_ms) {
    if (hold_running_ && gate_ == TARGET_GATE && (now_ms - hold_since_ms_) >= HOLD_MS) {
        solved_ = true;
    }
}

} // namespace ot::dam
