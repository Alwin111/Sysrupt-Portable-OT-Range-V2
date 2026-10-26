// SPDX-License-Identifier: Apache-2.0
#include "Substation.h"

#include <cstring>

namespace ot::sub {

void Substation::reset(uint32_t now_ms) {
    breaker_open_ = false;
    trip_confirmed_ = false;
    spont_trip_armed_ = false;
    interrog_lock_ = false;
    last_trip_orig_ = 0;
    interrog_cycle_start_ms_ = now_ms;
    solved_ = false;
}

void Substation::tick(uint32_t now_ms) {
    // The substation runs a periodic general interrogation; the lock is asserted for the first
    // INTERROG_WINDOW_MS of every INTERROG_PERIOD_MS. Commands that land in that window are dropped.
    const uint32_t phase = (now_ms - interrog_cycle_start_ms_) % INTERROG_PERIOD_MS;
    interrog_lock_ = phase < INTERROG_WINDOW_MS;
}

void Substation::updateSolved() {
    // Blackout achieved: the breaker is open AND the accepted trip carried the substation's own
    // trusted originator address (Last_Legit_Originator). Latched (set-only, like the other
    // challenges) so a later legitimate breaker close does not retract an already-captured flag.
    if (breaker_open_ && last_trip_orig_ == SUBSTATION_ORIG) {
        solved_ = true;
    }
}

CmdResult Substation::command(uint32_t ioa, uint8_t cot, uint8_t orig, bool on, uint32_t now_ms) {
    tick(now_ms); // keep the lock state current for this command

    if (ioa == IOA_TEST_BREAKER) {
        return CmdResult::Ignored; // decoy: positively confirmed but has no effect
    }
    if (ioa != IOA_BREAKER) {
        return CmdResult::Unknown;
    }
    if (interrog_lock_) {
        return CmdResult::Dropped; // silently dropped during the interrogation window
    }
    // The real breaker only moves on a proper activation from the trusted originator.
    if (cot != 6 || orig != SUBSTATION_ORIG) {
        return CmdResult::Alarm;
    }
    // IEC 60870-5 single command (C_SC_NA) polarity: SCS=0 (OFF) de-energises / TRIPS the breaker
    // (opens it); SCS=1 (ON) closes it. The player guide's walkthrough states the command is
    // "TRIP (OFF)", so an OFF single command must OPEN the breaker. (`on` carries the SCS bit.)
    const bool trip = !on;
    breaker_open_ = trip;
    if (trip) {
        trip_confirmed_ = true;
        last_trip_orig_ = orig;
    }
    updateSolved();
    return CmdResult::Accepted;
}

uint16_t Substation::flagWord(uint16_t index) const {
    if (!solved_ || index >= FLAG_LEN_OBJS) {
        return 0;
    }
    char bytes[FLAG_LEN_OBJS * 2] = {};
    const size_t cap = sizeof(bytes);
    for (size_t i = 0; i < cap && IEC104_FLAG[i] != '\0'; i++) {
        bytes[i] = IEC104_FLAG[i];
    }
    const uint8_t hi = (uint8_t)bytes[(size_t)index * 2];
    const uint8_t lo = (uint8_t)bytes[(size_t)index * 2 + 1];
    return (uint16_t)((hi << 8) | lo);
}

size_t Substation::enumerate(Point* out, size_t cap) const {
    size_t n = 0;
    auto add = [&](uint32_t ioa, uint8_t type, int32_t value) {
        if (n < cap) {
            out[n++] = Point { ioa, type, value };
        }
    };
    // Single points (type 1 = M_SP_NA)
    add(IOA_TEST_BREAKER, 1, 0);
    add(IOA_BREAKER, 1, breaker_open_ ? 1 : 0);
    add(IOA_INTERROG_LOCK, 1, interrog_lock_ ? 1 : 0);
    add(IOA_TRIP_CONFIRMED, 1, trip_confirmed_ ? 1 : 0);
    add(IOA_SPONT_TRIP_ARMED, 1, spont_trip_armed_ ? 1 : 0);
    // Measured values (type 11 = M_ME_NB, scaled int)
    add(IOA_LAST_TRIP_ORIG, 11, last_trip_orig_);
    add(IOA_LAST_LEGIT_ORIG, 11, SUBSTATION_ORIG);
    // Decoy feeders
    for (int i = 0; i < DECOY_COUNT; i++) {
        add(IOA_DECOY_BASE + i, 1, (i & 1));
    }
    // Flag object block (reads 0 until solved)
    for (uint16_t i = 0; i < FLAG_LEN_OBJS; i++) {
        add(IOA_FLAG_BASE + i, 11, (int32_t)(int16_t)flagWord(i));
    }
    return n;
}

} // namespace ot::sub
