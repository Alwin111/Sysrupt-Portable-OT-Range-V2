// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sim/Defensive.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Owns the shared Defensive investigation state for the three web-driven Defensive challenges
// (#07 Historian Forensics, #09 Trojan Historian, #10 Device Takeover): the three completion-predicate
// State objects, a mutex guarding them, and a tick task that advances the Trojan Historian stable-hold
// timer. The web surface (net/DefWeb) is a thin view that reads evidence and drives these predicates.
// Idempotent start(). No sockets of its own - it rides the shared :80 HTTP server (OtHttp).
namespace ot {

class DefService {
public:
    static DefService& instance();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    bool lock();
    void unlock();
    uint32_t nowMs() const;

    def::forensics::State& forensics() { return forensics_; }
    def::trojan::State& trojan() { return trojan_; }
    def::takeover::State& takeover() { return takeover_; }

private:
    DefService() = default;
    static void tickTaskEntry(void* arg);

    def::forensics::State forensics_;
    def::trojan::State trojan_;
    def::takeover::State takeover_;

    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<bool> running_ { false };
    TaskHandle_t tickTask_ = nullptr;
};

} // namespace ot
