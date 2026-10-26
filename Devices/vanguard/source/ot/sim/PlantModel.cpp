// SPDX-License-Identifier: Apache-2.0
#include "PlantModel.h"
#include "PlantConfig.h"

#include <cstring>

namespace ot {

namespace {

int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// First-order lag: move `current` a fraction (step/256) of the way toward `target`, with a floor
// of 1 so it always reaches the target instead of stalling just short of it.
uint16_t lagToward(uint16_t current, uint16_t target, int32_t step) {
    int32_t diff = (int32_t)target - (int32_t)current;
    int32_t delta = diff * step / 256;
    if (delta == 0 && diff != 0) {
        delta = diff > 0 ? 1 : -1;
    }
    return (uint16_t)clampi((int32_t)current + delta, 0, cfg::PERMILLE_MAX);
}

} // namespace

void PlantModel::reset(uint32_t now_ms, Challenge challenge) {
    std::memset(holding_, 0, sizeof(holding_));
    std::memset(input_, 0, sizeof(input_));

    challenge_ = challenge;
    last_tick_ms_ = now_ms;

    resetReactor(now_ms);
    resetMaintenance(now_ms);

    // Cleared only on a full reset (a fresh player), not on the per-subsystem resets above.
    expiry_read_ = false;
    ch2_lockout_until_ms_ = 0;
}

// Reactor + Challenge 1 only. Used on a fresh start and after a scram recovery, so a reactor
// excursion never wipes the independent Challenge 2 maintenance progress.
void PlantModel::resetReactor(uint32_t now_ms) {
    holding_[reg::HR_ROD_POSITION] = cfg::INIT_ROD;
    holding_[reg::HR_WATER_FLOW] = cfg::INIT_WATER_FLOW;
    holding_[reg::HR_PRESSURE_VALVE] = cfg::INIT_PRESSURE_VALVE;
    holding_[reg::HR_TURBINE] = cfg::INIT_TURBINE;

    input_[reg::IR_CORE_TEMP] = (uint16_t)cfg::INIT_CORE_TEMP;
    input_[reg::IR_NEUTRON_FLUX] = cfg::INIT_ROD;
    input_[reg::IR_COOLANT_FLOW] = cfg::INIT_WATER_FLOW;
    input_[reg::IR_TURBINE_RPM] = cfg::INIT_TURBINE;
    input_[reg::IR_PLANT_STATE] = reg::STATE_RUNNING;

    safe_since_ms_ = now_ms;
    safe_timer_running_ = false;
    ch1_solved_ = false;
    scram_ = false;
    scram_until_ms_ = 0;

    setFlag(reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN, cfg::CH1_FLAG, false);
    recomputeDerived();
    updateSafeBand();
}

// Challenge 2 maintenance subsystem only: supervisor unlock closed, maintenance rod and the
// reconciliation loop's "last authorised" value back at the baseline, progress cleared.
void PlantModel::resetMaintenance(uint32_t now_ms) {
    holding_[reg::HR_SUP_UNLOCK] = 0;
    holding_[reg::HR_MAINT_ROD] = cfg::CH2_RECON_BASELINE;
    last_authorised_ = cfg::CH2_RECON_BASELINE;
    input_[reg::IR_LAST_AUTHORISED] = cfg::CH2_RECON_BASELINE;
    input_[reg::IR_LAST_CHECK_TIME] = 0;
    input_[reg::IR_LAST_COMMIT_TIME] = 0;
    input_[reg::IR_UNLOCK_EXPIRY] = 0;
    input_[reg::IR_CH2_FLAGCOND] = 0;

    unlock_active_ = false;
    prev_unlock_cmd_ = false;
    unlock_deadline_ms_ = 0;
    last_check_ms_ = now_ms;
    last_commit_ms_ = now_ms;
    ch2_hold_running_ = false;
    ch2_hold_since_ms_ = 0;
    ch2_solved_ = false;
    abuse_window_start_ms_ = now_ms;
    abuse_writes_ = 0;

    setFlag(reg::IR_CH2_FLAG, reg::IR_CH2_FLAG_LEN, cfg::CH2_FLAG, false);
}

void PlantModel::tick(uint32_t now_ms) {
    uint32_t dt_ms = now_ms - last_tick_ms_;
    last_tick_ms_ = now_ms;
    if (dt_ms > cfg::DT_CLAMP_MS) {
        dt_ms = cfg::DT_CLAMP_MS;
    }

    if (scram_) {
        // During a scram the rods slam in and the reactor cools; Challenge 2's maintenance subsystem
        // is independent, so only the reactor resets when the scram clears.
        holding_[reg::HR_ROD_POSITION] = 0;
        recomputeDerived();
        if ((int32_t)(now_ms - scram_until_ms_) >= 0) {
            resetReactor(now_ms);
        }
        return;
    }

    // ---- Actuator lag ----
    input_[reg::IR_NEUTRON_FLUX] = lagToward(input_[reg::IR_NEUTRON_FLUX], holding_[reg::HR_ROD_POSITION], cfg::FLUX_LAG);
    input_[reg::IR_COOLANT_FLOW] = lagToward(input_[reg::IR_COOLANT_FLOW], holding_[reg::HR_WATER_FLOW], cfg::COOLANT_LAG);
    input_[reg::IR_TURBINE_RPM] = lagToward(input_[reg::IR_TURBINE_RPM], holding_[reg::HR_TURBINE], cfg::TURBINE_LAG);

    // ---- Core thermal integration ----
    int32_t flux = input_[reg::IR_NEUTRON_FLUX];
    int32_t coolant = input_[reg::IR_COOLANT_FLOW];
    int32_t rate_per_s = (cfg::HEAT_PER_FLUX * flux - cfg::COOL_PER_FLOW * coolant - cfg::AMBIENT_LOSS) / 1000; // tenths degC/s
    int32_t temp = (int16_t)input_[reg::IR_CORE_TEMP];
    temp += rate_per_s * (int32_t)dt_ms / 1000;
    temp = clampi(temp, cfg::CORE_TEMP_MIN, cfg::CORE_TEMP_MAX);
    input_[reg::IR_CORE_TEMP] = (uint16_t)(int16_t)temp;

    recomputeDerived();

    if (temp >= cfg::SCRAM_TEMP) {
        scram(now_ms);
        return;
    }

    updateSafeBand();

    // Both challenges are live on the one running reactor: the safe-band hold (Challenge 1) and the
    // TOCTOU unlock/override (Challenge 2) are solvable in the same session, in any order.
    updateChallenge1(now_ms);
    updateChallenge2(now_ms);
}

void PlantModel::recomputeDerived() {
    int32_t temp = (int16_t)input_[reg::IR_CORE_TEMP];
    int32_t valve = holding_[reg::HR_PRESSURE_VALVE];
    int32_t pressure = cfg::PRESSURE_BASE + (temp - cfg::PRESSURE_TEMP_REF) * cfg::PRESSURE_K_P / 100 - valve * cfg::PRESSURE_K_VALVE / 100;
    pressure = clampi(pressure, cfg::PRESSURE_MIN, cfg::PRESSURE_MAX);
    input_[reg::IR_PRESSURE] = (uint16_t)pressure;
}

void PlantModel::updateSafeBand() {
    int32_t temp = (int16_t)input_[reg::IR_CORE_TEMP];
    int32_t pressure = input_[reg::IR_PRESSURE];
    int32_t flux = input_[reg::IR_NEUTRON_FLUX];
    uint16_t bits = 0;
    if (temp >= cfg::TEMP_SAFE_LO && temp <= cfg::TEMP_SAFE_HI) bits |= reg::SAFE_TEMP;
    if (pressure >= cfg::PRESSURE_SAFE_LO && pressure <= cfg::PRESSURE_SAFE_HI) bits |= reg::SAFE_PRESSURE;
    if (flux >= cfg::FLUX_SAFE_LO && flux <= cfg::FLUX_SAFE_HI) bits |= reg::SAFE_FLUX;
    input_[reg::IR_SAFE_BAND] = bits;
}

void PlantModel::updateChallenge1(uint32_t now_ms) {
    const bool safe = input_[reg::IR_SAFE_BAND] == reg::SAFE_ALL;
    if (safe) {
        if (!safe_timer_running_) {
            safe_timer_running_ = true;
            safe_since_ms_ = now_ms;
        } else if (!ch1_solved_ && (now_ms - safe_since_ms_) >= cfg::CH1_HOLD_MS) {
            ch1_solved_ = true;
        }
    } else {
        // Leaving the band re-locks the flag: it must be held continuously.
        safe_timer_running_ = false;
        ch1_solved_ = false;
    }
    setFlag(reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN, cfg::CH1_FLAG, ch1_solved_);
}

void PlantModel::updateChallenge2(uint32_t now_ms) {
    // 1) The supervisor unlock stays effective only until its deadline; then it auto-resets to 0.
    if (unlock_active_ && (int32_t)(now_ms - unlock_deadline_ms_) >= 0) {
        unlock_active_ = false;
        holding_[reg::HR_SUP_UNLOCK] = 0;
    }

    // 2) The reconciliation loop. Each tick it checks the maintenance rod against the value it holds
    //    as legitimate. If they differ it either LEGITIMISES the change (if the unlock was open when
    //    it runs -- the TOCTOU: a write committed inside the window is treated as authorised) or
    //    REVERTS the rod back to the last authorised value.
    last_check_ms_ = now_ms;
    if (holding_[reg::HR_MAINT_ROD] != last_authorised_) {
        if (unlock_active_) {
            last_authorised_ = holding_[reg::HR_MAINT_ROD]; // newly legitimised
        } else {
            holding_[reg::HR_MAINT_ROD] = last_authorised_; // reverted
        }
        last_commit_ms_ = now_ms;
    }

    // 3) Publish the reconciliation telemetry (read-only; players fuzz these addresses to find them).
    input_[reg::IR_LAST_CHECK_TIME] = (uint16_t)(last_check_ms_ & 0xFFFF);
    input_[reg::IR_LAST_COMMIT_TIME] = (uint16_t)(last_commit_ms_ & 0xFFFF);
    input_[reg::IR_UNLOCK_EXPIRY] = unlock_active_ ? (uint16_t)(unlock_deadline_ms_ - now_ms) : 0;
    input_[reg::IR_LAST_AUTHORISED] = last_authorised_;

    // 4) Solve condition: the maintenance rod is legitimised at its maximum and that state holds
    //    continuously for CH2_HOLD_MS (the reconciliation loop is no longer reverting it).
    const bool at_max = holding_[reg::HR_MAINT_ROD] >= cfg::CH2_ROD_MAX &&
                        last_authorised_ >= cfg::CH2_ROD_MAX;
    if (at_max) {
        if (!ch2_hold_running_) {
            ch2_hold_running_ = true;
            ch2_hold_since_ms_ = now_ms;
        } else if (!ch2_solved_ && (now_ms - ch2_hold_since_ms_) >= cfg::CH2_HOLD_MS) {
            ch2_solved_ = true;
        }
    } else {
        ch2_hold_running_ = false;
        ch2_solved_ = false;
    }
    input_[reg::IR_CH2_FLAGCOND] = ch2_solved_ ? 1 : 0;
    setFlag(reg::IR_CH2_FLAG, reg::IR_CH2_FLAG_LEN, cfg::CH2_FLAG, ch2_solved_);
}

void PlantModel::scram(uint32_t now_ms) {
    // A reactor excursion: this only affects the reactor and Challenge 1. Challenge 2's maintenance
    // state is deliberately left untouched so the two challenges stay independent.
    scram_ = true;
    scram_until_ms_ = now_ms + cfg::SCRAM_MS;
    input_[reg::IR_PLANT_STATE] = reg::STATE_SCRAM;
    input_[reg::IR_SAFE_BAND] = 0;
    safe_timer_running_ = false;
    ch1_solved_ = false;
    setFlag(reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN, cfg::CH1_FLAG, false);
}

void PlantModel::setFlag(uint16_t base, uint16_t len_regs, const char* flag, bool revealed) {
    const size_t capacity = (size_t)len_regs * 2;
    char bytes[64] = {};
    if (revealed) {
        std::strncpy(bytes, flag, capacity); // leaves the remainder zero; never over-reads `flag`
    }
    for (uint16_t i = 0; i < len_regs; i++) {
        const uint8_t hi = (uint8_t)bytes[(size_t)i * 2];
        const uint8_t lo = (uint8_t)bytes[(size_t)i * 2 + 1];
        input_[base + i] = (uint16_t)((hi << 8) | lo);
    }
}

// ---- Register access ----

bool PlantModel::isWritable(uint16_t addr) const {
    switch (addr) {
        case reg::HR_ROD_POSITION:
        case reg::HR_WATER_FLOW:
        case reg::HR_PRESSURE_VALVE:
        case reg::HR_TURBINE:
        case reg::HR_SUP_UNLOCK:
        case reg::HR_MAINT_ROD:
            return true;
        default:
            // The reconciliation telemetry and everything else is read-only: discoverable by
            // fuzzing, but not writable.
            return false;
    }
}

uint16_t PlantModel::readHolding(uint16_t addr) const {
    return addr < reg::HR_COUNT ? holding_[addr] : 0;
}

uint16_t PlantModel::readInput(uint16_t addr) const {
    // Reading the hidden expiry register is what tells the player they have found the timing block;
    // it latches the HMI's "Step one done" indicator (a deliberate read side effect).
    if (addr == reg::IR_UNLOCK_EXPIRY) {
        expiry_read_ = true;
    }
    return addr < reg::IR_COUNT ? input_[addr] : 0;
}

bool PlantModel::writeHolding(uint16_t addr, uint16_t value, uint32_t now_ms) {
    if (addr >= reg::HR_COUNT || !isWritable(addr)) {
        return false;
    }

    // Supervisor unlock coil: a rising edge opens the maintenance window (and counts toward abuse).
    if (addr == reg::HR_SUP_UNLOCK) {
        const bool cmd = value != 0;
        // While locked out after abuse, unlock requests are ignored.
        if ((int32_t)(now_ms - ch2_lockout_until_ms_) < 0) {
            prev_unlock_cmd_ = cmd;
            holding_[reg::HR_SUP_UNLOCK] = 0;
            return true;
        }
        if (cmd && !prev_unlock_cmd_) {
            if ((now_ms - abuse_window_start_ms_) > cfg::CH2_ABUSE_WINDOW_MS) {
                abuse_window_start_ms_ = now_ms;
                abuse_writes_ = 0;
            }
            if (++abuse_writes_ > cfg::CH2_ABUSE_MAX_WRITES) {
                // Hammering the unlock trips a maintenance reset + lockout (can't brute-force it).
                resetMaintenance(now_ms);
                ch2_lockout_until_ms_ = now_ms + cfg::CH2_LOCKOUT_MS;
                prev_unlock_cmd_ = cmd;
                return true;
            }
            unlock_active_ = true;
            unlock_deadline_ms_ = now_ms + cfg::CH2_UNLOCK_WINDOW_MS;
        }
        prev_unlock_cmd_ = cmd;
        holding_[reg::HR_SUP_UNLOCK] = cmd ? 1 : 0;
        return true;
    }

    // Maintenance rod: a write is ACCEPTED ONLY while the unlock is currently open; otherwise it is
    // rejected (the caller surfaces a Modbus exception).
    if (addr == reg::HR_MAINT_ROD) {
        if (!unlock_active_) {
            return false;
        }
        holding_[reg::HR_MAINT_ROD] = value > cfg::CH2_ROD_MAX ? cfg::CH2_ROD_MAX : value;
        return true;
    }

    // Reactor actuator setpoints are permille; clamp so a wild write can't push the physics out of
    // range.
    if (addr == reg::HR_ROD_POSITION || addr == reg::HR_WATER_FLOW ||
        addr == reg::HR_PRESSURE_VALVE || addr == reg::HR_TURBINE) {
        if (value > cfg::PERMILLE_MAX) value = cfg::PERMILLE_MAX;
    }
    holding_[addr] = value;
    return true;
}

// ---- S7 byte views (big-endian: register N at byte offset 2N, MSB first) ----

bool PlantModel::readDb1(size_t off, size_t len, uint8_t* dst) const {
    if (off + len > kDb1Bytes) return false;
    for (size_t i = 0; i < len; i++) {
        const size_t byte = off + i;
        const uint16_t word = holding_[byte / 2];
        dst[i] = (byte & 1) ? (uint8_t)(word & 0xFF) : (uint8_t)(word >> 8);
    }
    return true;
}

bool PlantModel::readDb2(size_t off, size_t len, uint8_t* dst) const {
    if (off + len > kDb2Bytes) return false;
    for (size_t i = 0; i < len; i++) {
        const size_t byte = off + i;
        const uint16_t word = input_[byte / 2];
        dst[i] = (byte & 1) ? (uint8_t)(word & 0xFF) : (uint8_t)(word >> 8);
    }
    return true;
}

bool PlantModel::writeDb1(size_t off, size_t len, const uint8_t* src, uint32_t now_ms) {
    if (off + len > kDb1Bytes) return false;
    // A DB write can span several registers; apply it register by register through writeHolding so
    // the write policy and clamping are identical to Modbus. A write that touches a read-only
    // register is skipped for that register (not the whole request).
    size_t i = 0;
    while (i < len) {
        const size_t byte = off + i;
        const uint16_t addr = (uint16_t)(byte / 2);
        uint16_t word = holding_[addr];
        if (byte & 1) {
            word = (uint16_t)((word & 0xFF00) | src[i]);
            i += 1;
        } else if (i + 1 < len) {
            word = (uint16_t)((src[i] << 8) | src[i + 1]);
            i += 2;
        } else {
            word = (uint16_t)((src[i] << 8) | (word & 0x00FF));
            i += 1;
        }
        writeHolding(addr, word, now_ms);
    }
    return true;
}

PlantSnapshot PlantModel::snapshot() const {
    PlantSnapshot s {};
    s.challenge = challenge_;
    s.neutron_flux = input_[reg::IR_NEUTRON_FLUX];
    s.core_temp = (int16_t)input_[reg::IR_CORE_TEMP];
    s.pressure = input_[reg::IR_PRESSURE];
    s.coolant_flow = input_[reg::IR_COOLANT_FLOW];
    s.turbine_rpm = input_[reg::IR_TURBINE_RPM];
    s.rod_position = holding_[reg::HR_ROD_POSITION];
    s.water_flow = holding_[reg::HR_WATER_FLOW];
    s.pressure_valve = holding_[reg::HR_PRESSURE_VALVE];
    s.safe_band = input_[reg::IR_SAFE_BAND];
    s.scram = scram_;
    s.ch1_solved = ch1_solved_;
    s.ch2_solved = ch2_solved_;
    s.ch1_hold_ms = (safe_timer_running_ && last_tick_ms_ >= safe_since_ms_) ? (last_tick_ms_ - safe_since_ms_) : 0;
    s.maint_rod = holding_[reg::HR_MAINT_ROD];
    s.last_authorised = last_authorised_;
    s.unlock_active = unlock_active_;
    s.unlock_expiry_ms = input_[reg::IR_UNLOCK_EXPIRY];
    s.step1_done = expiry_read_;
    s.ch2_hold_ms = (ch2_hold_running_ && last_tick_ms_ >= ch2_hold_since_ms_) ? (last_tick_ms_ - ch2_hold_since_ms_) : 0;
    s.ch2_locked = (int32_t)(last_tick_ms_ - ch2_lockout_until_ms_) < 0;
    s.ch2_lockout_ms = s.ch2_locked ? (uint32_t)(ch2_lockout_until_ms_ - last_tick_ms_) : 0;
    return s;
}

} // namespace ot
