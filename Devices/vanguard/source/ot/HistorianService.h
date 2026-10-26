// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/Historian.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 6 service: owns one Historian, a mutex and a line-based TCP server on port 9000
// (historian query protocol). Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class HistorianService {
public:
    static HistorianService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    bool touched() const { return touched_.load(); }
    void markTouched() { touched_.store(true); }

    bool lock();
    void unlock();
    hist::Historian& system() { return sys_; }

private:
    HistorianService() = default;
    static void serverTaskEntry(void* arg);

    hist::Historian sys_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
