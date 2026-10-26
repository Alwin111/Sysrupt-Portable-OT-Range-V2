// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/OpcUaServer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 7 service: owns one OpcUaServer, a mutex and a TCP server on port 9005 (OPC-UA-lite
// browse/read). Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class OpcUaService {
public:
    static OpcUaService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    bool touched() const { return touched_.load(); }
    void markTouched() { touched_.store(true); }

    bool lock();
    void unlock();
    opcua::OpcUaServer& system() { return sys_; }

private:
    OpcUaService() = default;
    static void serverTaskEntry(void* arg);

    opcua::OpcUaServer sys_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
