// SPDX-License-Identifier: Apache-2.0
// Host-side tests for the pure plant simulation (no FreeRTOS/lwIP/LVGL). A closed-loop controller
// stands in for a player to prove each challenge is actually solvable, plus the lock/scram/abuse
// guards. Run: g++ -std=c++17 -I../source/ot ot_plant_tests.cpp ../source/ot/sim/PlantModel.cpp && ./a.out
#include "sim/PlantModel.h"
#include "sim/PlantConfig.h"
#include "sim/Registers.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace ot;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* file, int line, const char* expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    g_fail++;
}
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

// Reads the ASCII flag out of an input-register block.
static std::string readFlag(const PlantModel& m, uint16_t base, uint16_t len_regs) {
    std::string out;
    for (uint16_t i = 0; i < len_regs; i++) {
        uint16_t w = m.readInput(base + i);
        char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
        if (hi) out += hi;
        if (lo) out += lo;
    }
    return out;
}

// Nudges the actuators toward the safe band from the sensor readings -- what a player does once
// they've mapped the registers. Deliberately simple proportional control.
static void controlTowardSafe(PlantModel& m, uint32_t now_ms) {
    int32_t flux = m.readInput(reg::IR_NEUTRON_FLUX);
    int32_t temp = (int16_t)m.readInput(reg::IR_CORE_TEMP);
    int32_t pressure = m.readInput(reg::IR_PRESSURE);
    int32_t rod = m.readHolding(reg::HR_ROD_POSITION);
    int32_t water = m.readHolding(reg::HR_WATER_FLOW);
    int32_t valve = m.readHolding(reg::HR_PRESSURE_VALVE);

    const int32_t fluxTarget = (cfg::FLUX_SAFE_LO + cfg::FLUX_SAFE_HI) / 2;
    const int32_t tempTarget = (cfg::TEMP_SAFE_LO + cfg::TEMP_SAFE_HI) / 2;
    const int32_t pressureTarget = (cfg::PRESSURE_SAFE_LO + cfg::PRESSURE_SAFE_HI) / 2;

    // Proportional control: flux follows the rod, temperature is held with coolant flow, pressure
    // with the relief valve.
    rod += (fluxTarget - flux) / 8;
    water += (temp - tempTarget) / 6;
    valve += (pressure - pressureTarget) / 6;

    auto clampP = [](int32_t v) { return v < 0 ? 0 : (v > (int32_t)cfg::PERMILLE_MAX ? (int32_t)cfg::PERMILLE_MAX : v); };
    m.writeHolding(reg::HR_ROD_POSITION, (uint16_t)clampP(rod), now_ms);
    m.writeHolding(reg::HR_WATER_FLOW, (uint16_t)clampP(water), now_ms);
    m.writeHolding(reg::HR_PRESSURE_VALVE, (uint16_t)clampP(valve), now_ms);
}

static void testChallenge1() {
    PlantModel m;
    uint32_t t = 1000;
    m.reset(t, Challenge::One);

    // Flag is locked at the start.
    CHECK(readFlag(m, reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN).empty());

    // The plant heats up on its own if ignored: temperature should rise above the start value.
    uint32_t tIgnore = t;
    int16_t startTemp = (int16_t)m.readInput(reg::IR_CORE_TEMP);
    for (int i = 0; i < 20; i++) { tIgnore += cfg::TICK_MS; m.tick(tIgnore); }
    CHECK((int16_t)m.readInput(reg::IR_CORE_TEMP) > startTemp);

    // Now drive it into the safe band and hold. 120 s of simulated time is plenty.
    bool everSafe = false;
    std::string flag;
    for (int i = 0; i < 1200 && flag.empty(); i++) {
        t += cfg::TICK_MS;
        controlTowardSafe(m, t);
        m.tick(t);
        if (m.readInput(reg::IR_SAFE_BAND) == reg::SAFE_ALL) everSafe = true;
        flag = readFlag(m, reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN);
    }
    CHECK(everSafe);
    CHECK(flag == cfg::CH1_FLAG);
    CHECK(m.snapshot().ch1_solved);

    // Leaving the band re-locks the flag (withdraw the rod hard).
    for (int i = 0; i < 60; i++) {
        t += cfg::TICK_MS;
        m.writeHolding(reg::HR_ROD_POSITION, cfg::PERMILLE_MAX, t);
        m.tick(t);
    }
    CHECK(readFlag(m, reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN) != cfg::CH1_FLAG);
}

static void testChallenge1Scram() {
    PlantModel m;
    uint32_t t = 0;
    m.reset(t, Challenge::One);
    // Full withdrawal with no cooling must eventually scram.
    bool scrammed = false;
    for (int i = 0; i < 2000 && !scrammed; i++) {
        t += cfg::TICK_MS;
        m.writeHolding(reg::HR_ROD_POSITION, cfg::PERMILLE_MAX, t);
        m.writeHolding(reg::HR_WATER_FLOW, 0, t);
        m.tick(t);
        scrammed = m.snapshot().scram;
    }
    CHECK(scrammed);
    CHECK(m.readInput(reg::IR_PLANT_STATE) == reg::STATE_SCRAM);
    // After the scram time it returns to running.
    t += cfg::SCRAM_MS + cfg::TICK_MS;
    m.tick(t);
    CHECK(!m.snapshot().scram);
    CHECK(m.readInput(reg::IR_PLANT_STATE) == reg::STATE_RUNNING);
}

// Drives Challenge 2 from a fresh unlock to a legitimised maximum rod and holds it. Returns once the
// flag should be readable (or the time budget runs out). The optional keepReactorSafe callback is
// invoked each tick so the concurrent test can keep the reactor in the safe band during the hold.
static std::string solveChallenge2(PlantModel& m, uint32_t& t, void (*keepReactorSafe)(PlantModel&, uint32_t)) {
    // A rod write is rejected while the unlock is closed.
    CHECK(!m.writeHolding(reg::HR_MAINT_ROD, cfg::CH2_ROD_MAX, t));
    // Open the supervisor unlock; the hidden expiry register starts counting down.
    CHECK(m.writeHolding(reg::HR_SUP_UNLOCK, 1, t));
    t += cfg::TICK_MS; m.tick(t);
    CHECK(m.readInput(reg::IR_UNLOCK_EXPIRY) > 0);
    // Within the window, drive the maintenance rod to max; the reconciliation loop legitimises it.
    CHECK(m.writeHolding(reg::HR_MAINT_ROD, cfg::CH2_ROD_MAX, t));
    t += cfg::TICK_MS; m.tick(t);
    CHECK(m.readHolding(reg::HR_MAINT_ROD) == cfg::CH2_ROD_MAX);     // not reverted
    CHECK(m.readInput(reg::IR_LAST_AUTHORISED) == cfg::CH2_ROD_MAX); // now legitimate
    // Hold the legitimised max for CH2_HOLD_MS -> flag.
    std::string flag;
    for (int i = 0; i < 130 && flag.empty(); i++) {
        t += cfg::TICK_MS;
        if (keepReactorSafe) keepReactorSafe(m, t);
        m.tick(t);
        flag = readFlag(m, reg::IR_CH2_FLAG, reg::IR_CH2_FLAG_LEN);
    }
    return flag;
}

static void testChallenge2() {
    PlantModel m;
    uint32_t t = 10000;
    m.reset(t, Challenge::Two);

    CHECK(readFlag(m, reg::IR_CH2_FLAG, reg::IR_CH2_FLAG_LEN).empty());
    // Maintenance rod and the reconciliation loop both start at the baseline.
    CHECK(m.readHolding(reg::HR_MAINT_ROD) == cfg::CH2_RECON_BASELINE);
    CHECK(m.readInput(reg::IR_LAST_AUTHORISED) == cfg::CH2_RECON_BASELINE);

    const std::string flag = solveChallenge2(m, t, nullptr);
    CHECK(flag == cfg::CH2_FLAG);
    CHECK(m.snapshot().ch2_solved);
    CHECK(m.snapshot().step1_done); // reading the expiry register latched the HMI hint
}

static void testChallenge2Revert() {
    // Without an open unlock, the reconciliation loop reverts any lingering rod mismatch, and a
    // write that lands just after the window closes is undone.
    PlantModel m;
    uint32_t t = 0;
    m.reset(t, Challenge::Two);
    CHECK(m.writeHolding(reg::HR_SUP_UNLOCK, 1, t));         // open window (deadline = t + 2000)
    CHECK(m.writeHolding(reg::HR_MAINT_ROD, cfg::CH2_ROD_MAX, t)); // accepted while open
    t += cfg::CH2_UNLOCK_WINDOW_MS + cfg::TICK_MS;           // let the window expire before the tick
    m.tick(t);
    CHECK(m.readHolding(reg::HR_MAINT_ROD) == cfg::CH2_RECON_BASELINE); // reverted (not legitimised)
    CHECK(m.readInput(reg::IR_LAST_AUTHORISED) == cfg::CH2_RECON_BASELINE);
}

static void testChallenge2WindowExpiry() {
    PlantModel m;
    uint32_t t = 0;
    m.reset(t, Challenge::Two);
    CHECK(m.writeHolding(reg::HR_SUP_UNLOCK, 1, t));
    t += cfg::TICK_MS; m.tick(t);
    CHECK(m.readHolding(reg::HR_SUP_UNLOCK) == 1);
    // Past the window the unlock auto-resets to 0 and rod writes are rejected again.
    t += cfg::CH2_UNLOCK_WINDOW_MS + cfg::TICK_MS;
    m.tick(t);
    CHECK(m.readHolding(reg::HR_SUP_UNLOCK) == 0);
    CHECK(!m.writeHolding(reg::HR_MAINT_ROD, cfg::CH2_ROD_MAX, t));
}

static void testChallenge2Abuse() {
    PlantModel m;
    uint32_t t = 0;
    m.reset(t, Challenge::Two);
    // Hammering the unlock (toggling it on repeatedly) within the abuse window trips a maintenance
    // reset + lockout, so it can't be brute-forced.
    bool locked = false;
    for (int i = 0; i < cfg::CH2_ABUSE_MAX_WRITES + 3 && !locked; i++) {
        m.writeHolding(reg::HR_SUP_UNLOCK, 0, t);
        t += cfg::TICK_MS; m.tick(t);
        m.writeHolding(reg::HR_SUP_UNLOCK, 1, t);
        t += cfg::TICK_MS; m.tick(t);
        locked = m.snapshot().ch2_locked;
    }
    CHECK(locked);
    // While locked, the unlock will not open (so a rod write stays rejected).
    CHECK(!m.writeHolding(reg::HR_MAINT_ROD, cfg::CH2_ROD_MAX, t));
}

static void keepSafe(PlantModel& m, uint32_t t) {
    int32_t temp = (int16_t)m.readInput(reg::IR_CORE_TEMP);
    int32_t water = 486 + (8 * (temp - 3100)) / 10;
    if (water < 0) water = 0;
    if (water > (int32_t)cfg::PERMILLE_MAX) water = cfg::PERMILLE_MAX;
    m.writeHolding(reg::HR_ROD_POSITION, 475, t);
    m.writeHolding(reg::HR_PRESSURE_VALVE, 0, t);
    m.writeHolding(reg::HR_WATER_FLOW, (uint16_t)water, t);
}

// The badge runs both challenges live on one reactor: prove CH1 and CH2 are both solvable on the
// same running PlantModel instance without one's mechanic blocking the other.
static void testBothChallengesConcurrent() {
    PlantModel m;
    uint32_t t = 5000;
    m.reset(t, Challenge::One);

    // Solve CH1 first by holding the safe band (fixed rod + proportional coolant).
    std::string ch1;
    for (int i = 0; i < 1200 && ch1.empty(); i++) {
        t += cfg::TICK_MS;
        keepSafe(m, t);
        m.tick(t);
        ch1 = readFlag(m, reg::IR_CH1_FLAG, reg::IR_CH1_FLAG_LEN);
    }
    CHECK(ch1 == cfg::CH1_FLAG);

    // Now, on the SAME running plant, solve CH2 while keeping the reactor safe through the 10 s hold.
    const std::string ch2 = solveChallenge2(m, t, keepSafe);
    CHECK(ch2 == cfg::CH2_FLAG);
    CHECK(m.snapshot().ch2_solved);
}

static void testRegisterPolicy() {
    PlantModel m;
    m.reset(0, Challenge::One);
    // Unknown/telemetry holding addresses are read-only.
    CHECK(!m.writeHolding(50, 1, 0));            // in range but not a writable actuator
    CHECK(!m.writeHolding(reg::HR_COUNT, 1, 0)); // out of range
    // The maintenance rod is rejected while the unlock is closed.
    CHECK(!m.writeHolding(reg::HR_MAINT_ROD, cfg::CH2_ROD_MAX, 0));
    // Actuator writes are clamped to permille.
    CHECK(m.writeHolding(reg::HR_ROD_POSITION, 50000, 0));
    CHECK(m.readHolding(reg::HR_ROD_POSITION) == cfg::PERMILLE_MAX);

    // S7 DB view mirrors the registers big-endian (rod holds 0x0234 = 564, within permille).
    m.writeHolding(reg::HR_ROD_POSITION, 0x0234, 0);
    uint8_t buf[4] = {};
    CHECK(m.readDb1(reg::HR_ROD_POSITION * 2, 2, buf));
    CHECK(buf[0] == 0x02 && buf[1] == 0x34);
    // Out-of-range DB read fails.
    CHECK(!m.readDb1(PlantModel::kDb1Bytes, 2, buf));
    // A DB write applies through the same policy (rod clamped).
    uint8_t wide[2] = { 0xFF, 0xFF };
    CHECK(m.writeDb1(reg::HR_ROD_POSITION * 2, 2, wide, 0));
    CHECK(m.readHolding(reg::HR_ROD_POSITION) == cfg::PERMILLE_MAX);
}

int main() {
    testChallenge1();
    testChallenge1Scram();
    testChallenge2();
    testChallenge2Revert();
    testChallenge2WindowExpiry();
    testChallenge2Abuse();
    testBothChallengesConcurrent();
    testRegisterPolicy();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
