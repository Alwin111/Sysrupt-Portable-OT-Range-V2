// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/PumpSystem.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 3 service: owns one PumpSystem, a mutex, a tick task (alert lockout + open-hold) and a
// TCP server on port 7700 (custom pump protocol). Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class PumpService {
public:
    static PumpService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    bool targetOpen() const;
    bool alerted() const;
    bool touched() const { return touched_.load(); }
    void markTouched() { touched_.store(true); }

    bool lock();
    void unlock();
    pump::PumpSystem& system() { return sys_; }
    uint32_t nowMs() const;

private:
    PumpService() = default;
    static void tickTaskEntry(void* arg);
    static void serverTaskEntry(void* arg);

    pump::PumpSystem sys_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t tickTask_ = nullptr;
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
