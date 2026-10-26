// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// The register map of the simulated nuclear-plant HMI. One canonical store (PlantModel) holds
// every value; Modbus and S7comm are two protocol views onto it:
//   - Modbus holding registers (FC03/06/16) == holding[] below (player-writable)
//   - Modbus input registers   (FC04)        == input[]   below (read-only sensors/flags)
//   - S7 DB1 == holding[] serialized big-endian; S7 DB2 == input[] serialized big-endian
//
// All physical quantities are fixed-point unsigned so they survive the Modbus 16-bit register
// transport unchanged. "Permille" means 0..1000 == 0..100.0 %.
namespace ot::reg {

// ---- Holding registers: actuators and control (player-writable) ----
enum Holding : uint16_t {
    // Challenge 1 reactor actuators
    HR_ROD_POSITION = 0,   // permille. 0 = fully inserted (cold/safe), 1000 = fully withdrawn (hot)
    HR_WATER_FLOW = 1,     // permille pump setpoint
    HR_PRESSURE_VALVE = 2, // permille relief-valve opening
    HR_TURBINE = 3,        // permille turbine speed setpoint

    // Challenge 2 maintenance-control block.
    HR_SUP_UNLOCK = 20, // supervisor unlock coil: write 1 to open the maintenance window; the sim
                        // auto-resets it to 0 after CH2_UNLOCK_WINDOW_MS. Spamming it trips a reset.
    HR_MAINT_ROD = 21,  // maintenance rod position (0..CH2_ROD_MAX). A write is ACCEPTED ONLY while
                        // the unlock is active; otherwise it is rejected (Modbus exception).

    HR_COUNT = 96
};

// ---- Input registers: sensors, status and flags (read-only) ----
enum Input : uint16_t {
    IR_NEUTRON_FLUX = 0, // permille of nominal
    IR_CORE_TEMP = 1,    // tenths of degC (e.g. 3200 = 320.0 C)
    IR_PRESSURE = 2,     // tenths of bar
    IR_COOLANT_FLOW = 3, // permille measured coolant flow (tracks HR_WATER_FLOW with lag)
    IR_TURBINE_RPM = 4,  // permille measured turbine speed
    IR_SAFE_BAND = 5,    // bitmask, see SafeBand below. All bits set == plant in the safe band
    IR_PLANT_STATE = 6,  // 0 = running, 1 = scram/reset in progress (see PlantState)

    // Challenge 1 flag: reads as zero bytes until the plant is held in the safe band for
    // CH1_HOLD_MS, then the ASCII flag appears here (16 registers == 32 bytes).
    IR_CH1_FLAG = 32,
    IR_CH1_FLAG_LEN = 16,

    // Challenge 2 flag: reads as zero bytes until the maintenance rod is legitimised at maximum and
    // held for CH2_HOLD_MS (see PlantModel).
    IR_CH2_FLAG = 48,
    IR_CH2_FLAG_LEN = 16,

    // Challenge 2 reconciliation telemetry (read-only, discovered by fuzzing nearby addresses).
    IR_LAST_CHECK_TIME = 70,  // when the reconciliation loop last compared rod vs last-authorised
    IR_LAST_COMMIT_TIME = 71, // when it last committed a revert/legitimise (both: ms-since-boot low word)
    IR_UNLOCK_EXPIRY = 72,    // remaining ms the unlock stays effective (0 when closed). Hidden debug
                              // register: READING it flips the HMI's "Step one done" indicator.
    IR_LAST_AUTHORISED = 73,  // the rod value the reconciliation loop believes is legitimate
    IR_CH2_FLAGCOND = 74,     // 0/1: the challenge-2 solve condition is currently satisfied

    IR_COUNT = 96
};

// Bits in IR_SAFE_BAND
enum SafeBand : uint16_t {
    SAFE_TEMP = 1 << 0,
    SAFE_PRESSURE = 1 << 1,
    SAFE_FLUX = 1 << 2,
    SAFE_ALL = SAFE_TEMP | SAFE_PRESSURE | SAFE_FLUX
};

// Values of IR_PLANT_STATE
enum PlantState : uint16_t {
    STATE_RUNNING = 0,
    STATE_SCRAM = 1 // emergency reset after an unsafe excursion or brute-force abuse
};

} // namespace ot::reg
