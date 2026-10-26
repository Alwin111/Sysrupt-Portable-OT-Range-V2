// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Single source of truth for the plant simulation's tunables and the two challenge flags. Editing
// a flag or a threshold here is all that's needed to re-flag or re-balance the challenge.
namespace ot::cfg {

// ---- Flags ----
// Challenge 1: bring the reactor into the safe band and hold it (see CH1_HOLD_MS).
constexpr const char* CH1_FLAG = "flag{sc4d4_s3tp01nt_h3ld_s4fe}";
// Challenge 2: beat the unlock window and the last-authorised auto-revert loop.
constexpr const char* CH2_FLAG = "flag{t0ct0u_r0d_0verr1d3_pwn}";

// Flags are stored in 16-register (32-byte) input blocks, so they must fit.
constexpr size_t cstrlen(const char* str) { size_t n = 0; while (str[n]) n++; return n; }
static_assert(cstrlen(CH1_FLAG) <= 32, "CH1_FLAG must fit 16 input registers (32 bytes)");
static_assert(cstrlen(CH2_FLAG) <= 32, "CH2_FLAG must fit 16 input registers (32 bytes)");

// ---- Simulation cadence ----
constexpr uint32_t TICK_MS = 100;     // physics/challenge scan period
constexpr uint32_t DT_CLAMP_MS = 500; // a longer gap (e.g. after a stall) is treated as this much

// ---- Permille helpers ----
constexpr uint16_t PERMILLE_MAX = 1000;

// ---- Initial (deliberately unsafe) conditions ----
// Rod withdrawn far, weak cooling, valve shut: core temperature climbs. The player must act.
constexpr uint16_t INIT_ROD = 820;
constexpr uint16_t INIT_WATER_FLOW = 180;
constexpr uint16_t INIT_PRESSURE_VALVE = 0;
constexpr uint16_t INIT_TURBINE = 400;
constexpr int32_t INIT_CORE_TEMP = 2800; // 280.0 C

// ---- Lag (first-order tracking, per tick, in 1/256 steps toward target) ----
constexpr int32_t FLUX_LAG = 40;    // neutron flux follows rod position
constexpr int32_t COOLANT_LAG = 32; // measured coolant flow follows the pump setpoint
constexpr int32_t TURBINE_LAG = 24;

// ---- Core thermal model (per second, scaled by dt) ----
// dTemp = (HEAT_PER_FLUX*flux - COOL_PER_FLOW*coolant - AMBIENT) / 1000, in tenths of degC/s
constexpr int32_t HEAT_PER_FLUX = 240;
constexpr int32_t COOL_PER_FLOW = 210;
constexpr int32_t AMBIENT_LOSS = 12000; // constant background cooling term (scaled like the above)
constexpr int32_t CORE_TEMP_MIN = 200;  // 20.0 C
constexpr int32_t CORE_TEMP_MAX = 9000; // 900.0 C (hard ceiling; above SCRAM_TEMP it scrams)

// ---- Pressure model ----
// pressure = BASE + (temp - TEMP_REF)*K_P/100 - valve*K_VALVE/100, clamped >= 0
constexpr int32_t PRESSURE_BASE = 700; // 70.0 bar
constexpr int32_t PRESSURE_TEMP_REF = 3000;
constexpr int32_t PRESSURE_K_P = 35;
constexpr int32_t PRESSURE_K_VALVE = 40;
constexpr int32_t PRESSURE_MIN = 0;
constexpr int32_t PRESSURE_MAX = 3000; // 300.0 bar

// ---- Safe band ----
constexpr int32_t TEMP_SAFE_LO = 2900;  // 290.0 C
constexpr int32_t TEMP_SAFE_HI = 3300;  // 330.0 C
constexpr int32_t PRESSURE_SAFE_LO = 600;
constexpr int32_t PRESSURE_SAFE_HI = 1050;
constexpr int32_t FLUX_SAFE_LO = 300;
constexpr int32_t FLUX_SAFE_HI = 650;

// ---- Excursion / scram ----
constexpr int32_t SCRAM_TEMP = 3800; // 380.0 C -- an unsafe excursion scrams the plant
constexpr uint32_t SCRAM_MS = 4000;  // how long a scram/reset takes before running again

// ---- Challenge 1 ----
constexpr uint32_t CH1_HOLD_MS = 5000; // safe band must hold continuously this long

// ---- Challenge 2 (maintenance override: TOCTOU unlock + reconciliation loop) ----
// The supervisor unlock stays effective only this long after it is written, then auto-resets to 0.
// Players must discover the window by reading the hidden expiry/timestamp registers (fuzzing).
constexpr uint32_t CH2_UNLOCK_WINDOW_MS = 2000;
// The reconciliation loop's baseline "legitimate" rod position (also the maintenance rod's start).
constexpr uint16_t CH2_RECON_BASELINE = 10;
// The maintenance rod must be driven to this maximum position to request the override.
constexpr uint16_t CH2_ROD_MAX = 100;
// ...and the legitimised state (rod == CH2_ROD_MAX, reconciliation no longer reverting) must hold
// continuously this long before the flag is granted.
constexpr uint32_t CH2_HOLD_MS = 10000;
// Writing the unlock more than this many times within CH2_ABUSE_WINDOW_MS trips a maintenance reset.
constexpr uint8_t CH2_ABUSE_MAX_WRITES = 6;
constexpr uint32_t CH2_ABUSE_WINDOW_MS = 2000;
// After an abuse trip the maintenance controller is locked (ignores unlock requests) this long.
constexpr uint32_t CH2_LOCKOUT_MS = 3000;

} // namespace ot::cfg
