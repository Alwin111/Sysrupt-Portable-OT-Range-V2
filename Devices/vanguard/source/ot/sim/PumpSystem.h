// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 3 - Custom Pump Protocol. Pure logic (host-tested); the pump protocol (net/PumpProto)
// is a thin view onto this state.
//
// Scenario: a bespoke pump/valve control bus. A packet capture of normal commands is provided (a
// companion/CTF asset). The attacker must OPEN a specific flow-control valve and hold it open for
// HOLD_MS with no alert. Defences the capture reveals:
//   - Anti-replay: every command carries a monotonically increasing sequence number. Replaying a
//     captured frame (seq <= last accepted) raises an ALERT.
//   - Multi-command: a valve only opens after it has been SELECTed, then OPENed, in order (a lone
//     OPEN, or OPEN of a valve that wasn't selected, raises an ALERT).
//   - Decoys: several other pump/valve ids carry unrelated traffic in the capture.
// So a byte-for-byte replay fails; the attacker must craft a *fresh* SELECT+OPEN sequence (higher
// seq) for the target valve. An alert latches a short lockout and shuts the valve. Solve = target
// valve open, no active alert, held HOLD_MS; the flag block then reads the flag.
namespace ot::pump {

constexpr uint8_t TARGET_VALVE = 0x07;   // the flow-control valve to open
constexpr uint8_t DECOY_IDS[] = { 0x02, 0x05, 0x09, 0x0C }; // other feeders seen in the capture

constexpr uint8_t ACT_SELECT = 0x01;
constexpr uint8_t ACT_OPEN = 0x02;
constexpr uint8_t ACT_CLOSE = 0x03;

constexpr uint32_t HOLD_MS = 10000;      // target valve must stay open this long
constexpr uint32_t ALERT_LOCKOUT_MS = 6000; // an alert blocks progress this long
// Sequence numbers up to here are "already seen": this is the capture's high-water mark (the last/
// highest sequence that appears in the provided capture = 0x01EB = 491). A fresh connection's anti-
// replay floor starts here, so replaying any captured frame is rejected AND "the capture's last
// sequence + 1" (492) is the first fresh value the server accepts - the intended reasoning step, not a
// blind upward probe. (The counter is per-connection and never advanced past the capture by test
// traffic, so it is derivable from the capture alone.)
constexpr uint16_t BASELINE_SEQ = 491;

// Flag block (read via the protocol's READFLAG) - zero until solved.
constexpr uint16_t FLAG_LEN = 16;        // 16 words * 2 bytes = 32-byte flag
// Challenge-3 flag. Must fit 32 bytes.
constexpr const char* PUMP_FLAG = "flag{pump_s3q_cr4ft_n0_r3pl4y}";
constexpr size_t pump_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(pump_cstrlen(PUMP_FLAG) <= 32, "PUMP_FLAG must fit 16 flag words (32 bytes)");

enum class CmdResult {
    Ok,          // command accepted and applied
    Replay,      // seq not greater than the last accepted -> alert
    OutOfOrder,  // OPEN without a prior SELECT of that valve -> alert
    Bad          // malformed / unknown
};

class PumpSystem {
public:
    void reset(uint32_t now_ms);
    void tick(uint32_t now_ms);

    // Apply one command (the caller has already validated the sequence number, per-connection).
    CmdResult command(uint8_t target, uint8_t action, uint32_t now_ms);
    // Signal a detected replay (raises the alert that shuts the valve).
    void replay(uint32_t now_ms) { raiseAlert(now_ms); }

    bool targetOpen() const { return target_open_; }
    bool alerted() const { return alert_active_; }
    bool solved() const { return solved_; }
    uint16_t flagWord(uint16_t index) const;

private:
    void raiseAlert(uint32_t now_ms);

    uint8_t selected_ = 0;       // last SELECTed valve id (0 = none)
    bool target_open_ = false;
    bool alert_active_ = false;
    uint32_t alert_until_ms_ = 0;
    bool hold_running_ = false;
    uint32_t hold_since_ms_ = 0;
    bool solved_ = false;
};

} // namespace ot::pump
