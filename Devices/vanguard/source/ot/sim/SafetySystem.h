// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 5 - Safety-System Integrity (a Triton/Tricon-style SIS). Pure logic (host-tested); the
// TriStation-lite protocol (net/Tristation) is a thin view onto this state.
//
// Scenario (mirrors the real Triton attack): the SIS safety trip is enabled and the safety program
// is protected by a checksum reconciliation. To disable the trip the attacker must perform a valid
// PROGRAM DOWNLOAD, which requires: (1) the Program-Download lock to be open - it opens briefly on
// request then auto re-locks; (2) a rolling DEBUG KEY (hidden register, found by fuzzing, changes
// over time) to authorise the firmware-version spoof; and (3) a declared firmware version that
// matches Firmware_Compat_Flag so the checksum reconciliation accepts the new program (Checksum_Match
// stays true). A naive direct write to the trip is rejected. Solve = trip disabled AND Checksum_Match
// held true for CHECKSUM_HOLD_MS; the flag register block then reads the flag.
namespace ot::sis {

// ---- Registers (TriStation-lite address space) ----
constexpr uint16_t REG_TRIP = 0;            // SIS_Trip_Function (1 = armed/enabled)
constexpr uint16_t REG_LOCK = 1;            // Program_Download_Lock (write 1 to request; auto re-locks)
constexpr uint16_t REG_FW_COMPAT = 2;       // Firmware_Compat_Flag (declared fw a payload must match)
constexpr uint16_t REG_LAST_CHECKSUM = 3;   // Last_Validated_Checksum
constexpr uint16_t REG_CHECKSUM_MATCH = 4;  // Checksum_Match (1 = current logic matches validated)
constexpr uint16_t REG_LOCK_EXPIRY = 5;     // ms the download lock has left (0 when closed)
constexpr uint16_t REG_DEBUG_KEY = 42;      // hidden rolling key (found by fuzzing)
// Decoy diagnostic registers near the real key: they also change value over time but on a DIFFERENT
// cadence than REG_DEBUG_KEY and never equal the key, so a scan of the space returns several moving
// candidates and the attacker must test which one the controller's download actually accepts (a bare
// "take the first changing register" guess fails with BadKey).
constexpr uint16_t REG_DECOY_UPTIME = 40;   // free-running ~1 s counter (uptime-style)
constexpr uint16_t REG_DECOY_DIAG = 43;     // a second diagnostic that rolls on a ~2 s cadence
constexpr uint16_t REG_FLAG_BASE = 100;     // 16 registers of ASCII flag (zero until solved)
constexpr uint16_t FLAG_LEN_REGS = 16;
constexpr uint16_t REG_COUNT = 128;

// ---- Tunables ----
constexpr uint16_t FIRMWARE_COMPAT = 3008;      // Tricon 3008
constexpr uint16_t BASELINE_CHECKSUM = 0x5A3C;
constexpr uint32_t LOCK_WINDOW_MS = 1500;       // download lock stays open this long (tightened: scripted path)
constexpr uint32_t KEY_ROLL_MS = 5000;          // the debug key changes this often
constexpr uint32_t CHECKSUM_HOLD_MS = 5000;     // disabled-trip + valid-checksum must hold this long

// Challenge-5 flag (SIS trip disabled / Triton-style key download). Must fit 32 bytes.
constexpr const char* SIS_FLAG = "flag{s1s_tr1t0n_k3y_d0wnl04d}";
constexpr size_t sis_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(sis_cstrlen(SIS_FLAG) <= 32, "SIS_FLAG must fit 16 flag registers (32 bytes)");

enum class WriteResult { Ok, Rejected, ReadOnly };
enum class DownloadResult { Accepted, Locked, BadFirmware, BadKey };

class SafetySystem {
public:
    void reset(uint32_t now_ms);
    void tick(uint32_t now_ms);

    uint16_t read(uint16_t reg, uint32_t now_ms) const;        // now drives the rolling key / expiry
    WriteResult write(uint16_t reg, uint16_t value, uint32_t now_ms);
    DownloadResult programDownload(uint16_t firmware, uint16_t key, bool tripEnabled, uint32_t now_ms);

    uint16_t currentKey(uint32_t now_ms) const;
    bool tripEnabled() const { return trip_enabled_; }
    bool checksumMatch() const { return checksum_match_; }
    bool lockOpen() const { return lock_open_; }
    bool solved() const { return solved_; }

private:
    bool trip_enabled_ = true;
    bool lock_open_ = false;
    bool checksum_match_ = true;
    uint16_t last_checksum_ = BASELINE_CHECKSUM;
    uint32_t lock_deadline_ms_ = 0;
    bool hold_running_ = false;
    uint32_t hold_since_ms_ = 0;
    bool solved_ = false;
};

} // namespace ot::sis
