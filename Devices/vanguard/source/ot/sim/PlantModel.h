// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Registers.h"

#include <cstddef>
#include <cstdint>

// The simulated nuclear plant: one canonical register store plus the physics and the two
// challenge mechanics. Pure logic -- no FreeRTOS, lwIP or LVGL -- so it builds and is unit-tested
// on a host (see test/ot_plant_tests.cpp). Time is always passed in (now_ms = milliseconds since
// boot); the model never reads a clock itself.
//
// Thread-safety: this class does no locking. The service layer that owns the instance serializes
// every call (the sim tick and each protocol read/write) under one mutex -- see OtService.
namespace ot {

// The two challenges share one plant and HMI, but their control mechanics differ, so the plant
// runs in one mode at a time (the player picks a challenge, as in the original OT CTF firmware).
enum class Challenge {
    One, // stabilise the reactor and hold the safe band (CH1_HOLD_MS)
    Two  // beat the unlock window and the last-authorised auto-revert loop
};

// A small copy of what the HMI needs, taken under the lock and rendered without it.
struct PlantSnapshot {
    Challenge challenge;
    uint16_t neutron_flux;
    int16_t core_temp;     // tenths of degC
    uint16_t pressure;     // tenths of bar
    uint16_t coolant_flow;
    uint16_t turbine_rpm;
    uint16_t rod_position;
    uint16_t water_flow;
    uint16_t pressure_valve;
    uint16_t safe_band;    // reg::SafeBand bits
    bool scram;
    bool ch1_solved;
    bool ch2_solved;
    uint32_t ch1_hold_ms;  // how long the safe band has held so far (for the HMI progress bar)
    // Challenge 2 maintenance state (for the HMI)
    uint16_t maint_rod;        // maintenance rod position (0..CH2_ROD_MAX)
    uint16_t last_authorised;  // reconciliation loop's accepted rod value
    bool unlock_active;        // supervisor unlock currently open
    uint16_t unlock_expiry_ms; // ms the unlock has left (0 when closed)
    bool step1_done;           // the hidden expiry register has been read at least once
    uint32_t ch2_hold_ms;      // how long the legitimised max state has held so far
    bool ch2_locked;           // maintenance controller locked out after unlock abuse
    uint32_t ch2_lockout_ms;   // ms remaining on the unlock lockout (0 when not locked out) - HMI "retry in Ns"
};

class PlantModel {
public:
    // Puts the plant into its initial (deliberately unsafe) state for the given challenge and
    // clears all progress.
    void reset(uint32_t now_ms, Challenge challenge);
    Challenge challenge() const { return challenge_; }

    // Advances the physics and both challenge state machines to now_ms.
    void tick(uint32_t now_ms);

    // ---- Modbus-style access (the protocol adapters call these) ----
    // Reads never fail for an in-range address; out-of-range reads give 0.
    uint16_t readHolding(uint16_t addr) const;
    uint16_t readInput(uint16_t addr) const;
    // Returns false for an out-of-range or read-only address; a rejected write changes nothing.
    bool writeHolding(uint16_t addr, uint16_t value, uint32_t now_ms);

    // ---- S7 byte views (big-endian), DB1 = holding, DB2 = input ----
    // Copies len bytes starting at byte offset off into dst. Returns false if off+len is out of
    // range. The byte layout is register index N at byte offset 2N, most-significant byte first.
    bool readDb1(size_t off, size_t len, uint8_t* dst) const;
    bool readDb2(size_t off, size_t len, uint8_t* dst) const;
    bool writeDb1(size_t off, size_t len, const uint8_t* src, uint32_t now_ms);

    PlantSnapshot snapshot() const;

    static constexpr size_t kDb1Bytes = reg::HR_COUNT * 2;
    static constexpr size_t kDb2Bytes = reg::IR_COUNT * 2;

private:
    bool isWritable(uint16_t addr) const;
    void recomputeDerived();
    void updateSafeBand();
    void updateChallenge1(uint32_t now_ms);
    void updateChallenge2(uint32_t now_ms);
    void scram(uint32_t now_ms);
    void resetReactor(uint32_t now_ms);     // reactor + Challenge 1 only (leaves Challenge 2 intact)
    void resetMaintenance(uint32_t now_ms); // Challenge 2 maintenance subsystem only
    void setFlag(uint16_t base, uint16_t len_regs, const char* flag, bool revealed);

    uint16_t holding_[reg::HR_COUNT] = {};
    uint16_t input_[reg::IR_COUNT] = {};

    Challenge challenge_ = Challenge::One;
    uint32_t last_tick_ms_ = 0;

    // Challenge 1
    uint32_t safe_since_ms_ = 0;
    bool safe_timer_running_ = false;
    bool ch1_solved_ = false;

    // Challenge 2 (maintenance override)
    bool unlock_active_ = false;
    bool prev_unlock_cmd_ = false;
    uint32_t unlock_deadline_ms_ = 0;
    uint16_t last_authorised_ = 0;    // reconciliation loop's accepted rod value
    uint32_t last_check_ms_ = 0;      // last reconciliation check / commit timestamps
    uint32_t last_commit_ms_ = 0;
    bool ch2_hold_running_ = false;
    uint32_t ch2_hold_since_ms_ = 0;
    bool ch2_solved_ = false;
    mutable bool expiry_read_ = false; // the hidden expiry register has been read (sets "Step one done")
    // Abuse detection (brute-force unlock writes) + post-abuse lockout
    uint32_t abuse_window_start_ms_ = 0;
    uint8_t abuse_writes_ = 0;
    uint32_t ch2_lockout_until_ms_ = 0;

    // Scram
    bool scram_ = false;
    uint32_t scram_until_ms_ = 0;
};

} // namespace ot
