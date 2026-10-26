// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "net/Modbus.h"
#include "net/OtHttp.h"
#include "net/RangeNetwork.h"
#include "sim/PlantModel.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

// Owns the running OT challenge: one PlantModel, the single mutex that serializes the sim tick and
// every protocol access, and the background tasks (plant tick + Modbus server, with S7comm added
// in the next slice). The on-badge HMI app starts it when the player opens a challenge and stops
// it on the way out.
namespace ot {

class OtService : public modbus::Handler {
public:
    static OtService& instance();

    // Starts (or restarts) the challenge: seeds the plant and launches the tasks. Idempotent per
    // challenge -- calling it again just reseeds.
    void start(Challenge challenge);
    void stop();
    bool running() const { return running_.load(); }
    Challenge challenge() const { return challenge_; }

    PlantSnapshot snapshot();

    // modbus::Handler -- each call takes the lock for its own short critical section.
    uint16_t readHolding(uint16_t addr) override;
    uint16_t readInput(uint16_t addr) override;
    bool writeHolding(uint16_t addr, uint16_t value) override;

    // Attacker-activity detection, fed by the Modbus server path (by function code) so the
    // scoreboard's stage grid reflects what the player actually did, not just that they joined.
    void noteModbus(uint8_t function_code);

    // Player presence gates the reactor physics: while nobody is connected the plant is frozen at
    // its (deliberately unsafe) start state so the attract screen doesn't scram-loop. The first join
    // reseeds a clean scenario and starts a new session (so the next player gets a fresh board).
    void setPlayerPresent(bool present);

    struct Metrics {
        uint32_t sessionId;  // increments on each fresh join; scoreboard resets its latches when it changes
        bool mappedInput;    // player has read input registers (FC04)
        bool mappedHolding;  // player has read holding registers (FC03)
        uint32_t reads;
        uint32_t writes;
    };
    Metrics metrics() const;

    // Latched "solved this session" for the challenge menu/view (1 = Setpoint Safety, 2 = Maintenance
    // Override). Cleared on a fresh player join.
    bool challengeSolved(int number) const;

    // Session reset: reseed the reactor to its clean (unsafe) start and clear the solved latches,
    // without tearing down the Wi-Fi AP / HTTP / Modbus tasks. Used by the Reset Session screen.
    void resetChallenges();

    // Current time base shared by the sim and the protocol writes.
    uint32_t nowMs() const;

    // Wi-Fi range status, for the HMI (SSID is fixed, see OtService.cpp).
    net::RangeNetworkStatus networkStatus();
    const char* ssid() const;

private:
    OtService() = default;

    bool lock();
    void unlock();
    static void simTaskEntry(void* arg);
    static void modbusTaskEntry(void* arg);

    PlantModel plant_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    Challenge challenge_ = Challenge::One;

    // Physics gating + attacker-activity detection.
    std::atomic<bool> active_ { false };        // plant physics run only while a player is present
    std::atomic<bool> playerPresent_ { false }; // previous presence, for edge detection
    std::atomic<uint32_t> sessionId_ { 0 };
    std::atomic<bool> mappedInput_ { false };
    std::atomic<bool> mappedHolding_ { false };
    std::atomic<uint32_t> reads_ { 0 };
    std::atomic<uint32_t> writes_ { 0 };
    std::atomic<bool> ch1_ever_ { false }; // latched "solved this session" for the menu
    std::atomic<bool> ch2_ever_ { false };
    uint32_t lastClientMs_ = 0; // last time a Wi-Fi station was associated (sim task only; debounce)

    TaskHandle_t simTask_ = nullptr;
    TaskHandle_t modbusTask_ = nullptr;

    net::RangeNetwork network_;
    net::OtHttp http_;
};

} // namespace ot
