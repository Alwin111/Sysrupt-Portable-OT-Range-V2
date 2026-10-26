// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/GooseSim.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Challenge 06 service: owns one GooseSim and a UDP socket on port goose::UDP_PORT. A publisher task
// broadcasts the normal (breaker-closed, fixed stNum, incrementing sqNum) GOOSE-like stream so a
// connected client can observe the APPID/counters; the receive loop feeds inbound datagrams to the
// subscriber logic and replies with an ACCEPTED/REJECTED status (and the flag once the forged
// breaker-open state change is accepted). Shares the Wi-Fi AP. Idempotent start().
namespace ot {

class GooseService {
public:
    static GooseService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool solved() const;
    bool breakerOpen() const;
    uint32_t lastStNum() const;
    bool touched() const { return touched_.load(); }

    bool lock();
    void unlock();
    goose::GooseSim& system() { return sys_; }

private:
    GooseService() = default;
    static void serverTaskEntry(void* arg); // one task: receive + periodic broadcast on one socket

    goose::GooseSim sys_;
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    std::atomic<bool> touched_ { false };
    TaskHandle_t serverTask_ = nullptr;
};

} // namespace ot
