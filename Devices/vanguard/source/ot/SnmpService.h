// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/SnmpDevice.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 08 service: owns one SnmpDevice and a UDP socket on port snmp::UDP_PORT (161). One task
// receives SNMP GET/GETNEXT/SET datagrams, answers them via the net/Snmp codec, and ticks the device
// so the recovery hold timer advances even without traffic. The flag is exposed through the device's
// recovery-flag OID once the field device has been restored to its last-good firmware and verified
// healthy. Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class SnmpService {
public:
    static SnmpService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    bool healthy() const;
    bool touched() const { return touched_.load(); }

    bool lock();
    void unlock();
    snmp::SnmpDevice& system() { return dev_; }

private:
    SnmpService() = default;
    static void serverTaskEntry(void* arg);

    snmp::SnmpDevice dev_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
