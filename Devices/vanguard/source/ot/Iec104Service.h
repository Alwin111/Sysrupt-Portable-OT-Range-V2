// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/Substation.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 4 service: owns one Substation, a mutex serialising the tick and every IEC-104 access,
// a tick task (advances the interrogation-lock cycle) and a TCP server on port 2404 (IEC-104). It
// shares the Wi-Fi AP that OtService already brought up. Started when the IEC-104 challenge is
// selected; idempotent.
namespace ot {

class Iec104Service {
public:
    static Iec104Service& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    // For the challenge menu/view.
    bool solved() const;
    bool breakerOpen() const;
    bool interrogationLock() const;
    bool touched() const { return touched_.load(); }  // a client has sent at least one frame
    void markTouched() { touched_.store(true); }

    // Serialised access used by the protocol handler.
    bool lock();
    void unlock();
    sub::Substation& substation() { return sub_; }
    uint32_t nowMs() const;

private:
    Iec104Service() = default;
    static void tickTaskEntry(void* arg);
    static void serverTaskEntry(void* arg);

    sub::Substation sub_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t tickTask_ = nullptr;
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
