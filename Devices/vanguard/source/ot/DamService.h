// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/DamSystem.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 05 service: owns one DamSystem, a mutex, a tick task (gate hold timer) and a TCP server
// on port 9003 (legacy modem emulator). Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class DamService {
public:
    static DamService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    uint8_t gate() const;
    bool atTarget() const;
    bool touched() const { return touched_.load(); }
    void markTouched() { touched_.store(true); }

    bool lock();
    void unlock();
    dam::DamSystem& system() { return sys_; }
    uint32_t nowMs() const;

private:
    DamService() = default;
    static void tickTaskEntry(void* arg);
    static void serverTaskEntry(void* arg);

    dam::DamSystem sys_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t tickTask_ = nullptr;
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
