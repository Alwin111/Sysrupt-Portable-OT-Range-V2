// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 4 - IEC-104 Substation. Pure logic (no FreeRTOS/lwIP), so it is host-unit-tested like
// PlantModel. The IEC-104 server (net/Iec104) is a thin protocol view onto this state.
//
// Scenario: a substation breaker is energised (closed). The attacker must trip it (open it) to cause
// a blackout, but a naive command fails:
//   - A single command (C_SC) to the real breaker is only accepted with cause-of-transmission
//     ACTIVATION (COT=6) AND the correct ORIGINATOR address - the substation's own trusted sender
//     (Last_Legit_Originator). Any other originator raises an ALARM and is rejected.
//   - During the periodic GENERAL INTERROGATION window (Interrogation_Lock = true) commands are
//     silently dropped.
//   - A nearby "testing" breaker IOA accepts commands but silently ignores them (no effect, no
//     alarm) - the decoy the player probes to learn the IOA scheme.
// Discovery: a general interrogation enumerates every point, including Last_Legit_Originator (the
// address to use) and Interrogation_Lock. Solve = breaker open AND last trip originator == the
// substation's legit originator; then the flag object block reads the flag.
namespace ot::sub {

// ---- Addressing ----
constexpr uint16_t COMMON_ADDR = 1;      // ASDU common address of the substation
constexpr uint8_t SUBSTATION_ORIG = 7;   // the trusted originator address (== Last_Legit_Originator)

// ---- Information object addresses (IOA) ----
constexpr uint32_t IOA_TEST_BREAKER = 1000;  // decoy: C_SC accepted but ignored (no alarm)
constexpr uint32_t IOA_BREAKER = 1001;       // real breaker: M_SP status + C_SC trip command
constexpr uint32_t IOA_INTERROG_LOCK = 2001; // M_SP, read-only (true during interrogation)
constexpr uint32_t IOA_LAST_TRIP_ORIG = 2002;// M_ME, read-only
constexpr uint32_t IOA_LAST_LEGIT_ORIG = 2003;// M_ME, read-only (the originator to use)
constexpr uint32_t IOA_TRIP_CONFIRMED = 2004;// M_SP, read-only
constexpr uint32_t IOA_SPONT_TRIP_ARMED = 2005;// M_SP
constexpr uint32_t IOA_DECOY_BASE = 3001;    // a few decoy single points (other feeders)
constexpr int DECOY_COUNT = 4;
constexpr uint32_t IOA_FLAG_BASE = 5000;     // 16 measured-value objects carry the flag (zero until solved)
constexpr uint16_t FLAG_LEN_OBJS = 16;       // 16 objects * 2 bytes = 32-byte flag

// ---- Timing ----
constexpr uint32_t INTERROG_PERIOD_MS = 12000; // a general-interrogation sweep starts this often
constexpr uint32_t INTERROG_WINDOW_MS = 2000;  // ...and holds the lock this long (commands dropped)
constexpr uint32_t SOLVE_HOLD_MS = 0;          // the flag is granted as soon as the trip is accepted

// Challenge-4 flag (breaker tripped / substation blackout over IEC-104). Must fit 32 bytes (16
// M_ME flag objects). Edit here to re-flag.
constexpr const char* IEC104_FLAG = "flag{1ec104_br34k3r_bl4ck0ut}";
constexpr size_t cstrlen_(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(cstrlen_(IEC104_FLAG) <= 32, "IEC104_FLAG must fit 16 flag objects (32 bytes)");

// Result of applying a single command (C_SC) to the substation.
enum class CmdResult {
    Accepted,   // taken by a real control point (positive confirmation)
    Ignored,    // taken by the testing decoy (positive confirmation, no effect)
    Dropped,    // silently dropped (interrogation lock active)
    Alarm,      // rejected and alarmed (wrong originator / bad cause)
    Unknown     // unknown IOA
};

// One enumerated point, for a general-interrogation response.
struct Point {
    uint32_t ioa;
    uint8_t type;     // IEC-104 type id (1 = M_SP_NA, 11 = M_ME_NB)
    int32_t value;    // single point 0/1, or measured value
};

class Substation {
public:
    void reset(uint32_t now_ms);
    void tick(uint32_t now_ms);

    // Apply a single command. cot = cause of transmission, orig = originator address, on = SCS.
    CmdResult command(uint32_t ioa, uint8_t cot, uint8_t orig, bool on, uint32_t now_ms);

    // General-interrogation enumeration. Fills up to `cap` points, returns the count.
    size_t enumerate(Point* out, size_t cap) const;

    // Direct reads (used by tests and the HMI status).
    bool breakerOpen() const { return breaker_open_; }
    bool interrogationLock() const { return interrog_lock_; }
    uint8_t lastTripOriginator() const { return last_trip_orig_; }
    bool solved() const { return solved_; }

    // Reads one 16-bit word of the flag object block (0 until solved), big-endian ASCII pairs.
    uint16_t flagWord(uint16_t index) const;

private:
    void updateSolved();

    bool breaker_open_ = false;      // false = closed/energised, true = open/tripped
    bool trip_confirmed_ = false;
    bool spont_trip_armed_ = false;
    bool interrog_lock_ = false;
    uint8_t last_trip_orig_ = 0;
    uint32_t interrog_cycle_start_ms_ = 0;
    bool solved_ = false;
};

} // namespace ot::sub
