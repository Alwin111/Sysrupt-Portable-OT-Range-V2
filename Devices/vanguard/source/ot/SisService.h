// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/SafetySystem.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 5 service: owns one SafetySystem, a mutex, a tick task (lock re-lock + solve hold) and a
// TCP server on port 1502 (TriStation-lite). Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class SisService {
public:
    static SisService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    bool tripEnabled() const;
    bool checksumMatch() const;
    bool touched() const { return touched_.load(); }
    void markTouched() { touched_.store(true); }

    bool lock();
    void unlock();
    sis::SafetySystem& system() { return sys_; }
    uint32_t nowMs() const;

private:
    SisService() = default;
    static void tickTaskEntry(void* arg);
    static void serverTaskEntry(void* arg);

    sis::SafetySystem sys_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t tickTask_ = nullptr;
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
