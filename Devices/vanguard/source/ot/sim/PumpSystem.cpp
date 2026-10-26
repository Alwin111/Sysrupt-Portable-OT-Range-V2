// SPDX-License-Identifier: Apache-2.0
#include "PumpSystem.h"

namespace ot::pump {

void PumpSystem::reset(uint32_t now_ms) {
    selected_ = 0;
    target_open_ = false;
    alert_active_ = false;
    alert_until_ms_ = 0;
    hold_running_ = false;
    hold_since_ms_ = 0;
    solved_ = false;
    (void)now_ms;
}

void PumpSystem::raiseAlert(uint32_t now_ms) {
    alert_active_ = true;
    alert_until_ms_ = now_ms + ALERT_LOCKOUT_MS;
    target_open_ = false; // an alert trips the valve shut
    selected_ = 0;
    hold_running_ = false;
}

void PumpSystem::tick(uint32_t now_ms) {
    if (alert_active_ && (int32_t)(now_ms - alert_until_ms_) >= 0) {
        alert_active_ = false;
    }
    if (target_open_ && !alert_active_) {
        if (!hold_running_) {
            hold_running_ = true;
            hold_since_ms_ = now_ms;
        } else if (!solved_ && (now_ms - hold_since_ms_) >= HOLD_MS) {
            solved_ = true;
        }
    } else {
        hold_running_ = false;
    }
}

CmdResult PumpSystem::command(uint8_t target, uint8_t action, uint32_t now_ms) {
    tick(now_ms);
    switch (action) {
        case ACT_SELECT:
            selected_ = target;
            return CmdResult::Ok;
        case ACT_OPEN:
            // A valve only opens if it was selected first (multi-command sequence).
            if (selected_ != target) {
                raiseAlert(now_ms);
                return CmdResult::OutOfOrder;
            }
            if (target == TARGET_VALVE) {
                target_open_ = true;
            }
            return CmdResult::Ok;
        case ACT_CLOSE:
            if (target == TARGET_VALVE) {
                target_open_ = false;
            }
            return CmdResult::Ok;
        default:
            return CmdResult::Bad;
    }
}

uint16_t PumpSystem::flagWord(uint16_t index) const {
    if (!solved_ || index >= FLAG_LEN) {
        return 0;
    }
    char bytes[FLAG_LEN * 2] = {};
    for (size_t i = 0; i < sizeof(bytes) && PUMP_FLAG[i] != '\0'; i++) {
        bytes[i] = PUMP_FLAG[i];
    }
    return (uint16_t)(((uint8_t)bytes[index * 2] << 8) | (uint8_t)bytes[index * 2 + 1]);
}

} // namespace ot::pump
