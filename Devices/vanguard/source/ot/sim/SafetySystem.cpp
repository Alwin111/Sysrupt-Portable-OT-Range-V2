// SPDX-License-Identifier: Apache-2.0
#include "SafetySystem.h"

namespace ot::sis {

void SafetySystem::reset(uint32_t now_ms) {
    trip_enabled_ = true;
    lock_open_ = false;
    checksum_match_ = true;
    last_checksum_ = BASELINE_CHECKSUM;
    lock_deadline_ms_ = 0;
    hold_running_ = false;
    hold_since_ms_ = 0;
    solved_ = false;
    (void)now_ms;
}

uint16_t SafetySystem::currentKey(uint32_t now_ms) const {
    // A slowly-rolling 16-bit debug key, deterministic from the coarse time epoch.
    const uint32_t epoch = now_ms / KEY_ROLL_MS;
    return (uint16_t)((epoch * 40503u + 0xC0DEu) & 0xFFFF);
}

void SafetySystem::tick(uint32_t now_ms) {
    if (lock_open_ && (int32_t)(now_ms - lock_deadline_ms_) >= 0) {
        lock_open_ = false; // the download lock re-locks quickly
    }
    // Solve condition: the trip is disabled AND the program checksum validates, held continuously.
    if (!trip_enabled_ && checksum_match_) {
        if (!hold_running_) {
            hold_running_ = true;
            hold_since_ms_ = now_ms;
        } else if (!solved_ && (now_ms - hold_since_ms_) >= CHECKSUM_HOLD_MS) {
            solved_ = true;
        }
    } else {
        hold_running_ = false;
    }
}

uint16_t SafetySystem::read(uint16_t reg, uint32_t now_ms) const {
    switch (reg) {
        case REG_TRIP: return trip_enabled_ ? 1 : 0;
        case REG_LOCK: return lock_open_ ? 1 : 0;
        case REG_FW_COMPAT: return FIRMWARE_COMPAT;
        case REG_LAST_CHECKSUM: return last_checksum_;
        case REG_CHECKSUM_MATCH: return checksum_match_ ? 1 : 0;
        case REG_LOCK_EXPIRY: return lock_open_ ? (uint16_t)(lock_deadline_ms_ - now_ms) : 0;
        case REG_DEBUG_KEY: return currentKey(now_ms);
        // Decoy diagnostics that also change value, but on a DIFFERENT cadence than the key and never
        // equal to it: a scan returns several moving candidates, so the first changing register found is
        // not guaranteed correct - the attacker must test which value the download actually accepts.
        case REG_DECOY_UPTIME: return (uint16_t)(now_ms / 1000u);                        // ~1 s free-running counter
        case REG_DECOY_DIAG:   return (uint16_t)(((now_ms / 2000u) * 7919u + 0x1234u) & 0xFFFF); // ~2 s cadence
        default: break;
    }
    if (reg >= REG_FLAG_BASE && reg < REG_FLAG_BASE + FLAG_LEN_REGS) {
        if (!solved_) return 0;
        char bytes[FLAG_LEN_REGS * 2] = {};
        for (size_t i = 0; i < sizeof(bytes) && SIS_FLAG[i] != '\0'; i++) {
            bytes[i] = SIS_FLAG[i];
        }
        const uint16_t idx = reg - REG_FLAG_BASE;
        return (uint16_t)(((uint8_t)bytes[idx * 2] << 8) | (uint8_t)bytes[idx * 2 + 1]);
    }
    return 0; // unknown/decoy registers read as zero
}

WriteResult SafetySystem::write(uint16_t reg, uint16_t value, uint32_t now_ms) {
    if (reg == REG_LOCK) {
        if (value != 0) {
            lock_open_ = true;
            lock_deadline_ms_ = now_ms + LOCK_WINDOW_MS;
        }
        return WriteResult::Ok;
    }
    if (reg == REG_TRIP) {
        // A direct write to the safety trip is refused: the only legitimate path is a validated
        // program download.
        return WriteResult::Rejected;
    }
    return WriteResult::ReadOnly;
}

DownloadResult SafetySystem::programDownload(uint16_t firmware, uint16_t key, bool tripEnabled, uint32_t now_ms) {
    tick(now_ms);
    if (!lock_open_) {
        return DownloadResult::Locked;
    }
    if (firmware != FIRMWARE_COMPAT) {
        return DownloadResult::BadFirmware;
    }
    if (key != currentKey(now_ms)) {
        return DownloadResult::BadKey;
    }
    // Valid download: the new program is adopted and the checksum reconciliation accepts it.
    trip_enabled_ = tripEnabled;
    last_checksum_ = (uint16_t)(BASELINE_CHECKSUM ^ (tripEnabled ? 0x0000 : 0x0F0F));
    checksum_match_ = true;
    return DownloadResult::Accepted;
}

} // namespace ot::sis
