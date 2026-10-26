// SPDX-License-Identifier: Apache-2.0
#include "SnmpService.h"

#include "net/Snmp.h"

#include <tactility/log.h>

#include <lwip/sockets.h>

#include <cstdio>
#include <cstring>

namespace ot {

namespace {
constexpr auto* TAG = "Snmp";
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
} // namespace

SnmpService& SnmpService::instance() {
    static SnmpService service;
    return service;
}

bool SnmpService::lock() { return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE; }
void SnmpService::unlock() { xSemaphoreGive(mutex_); }

void SnmpService::start() {
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    if (running_.load()) { LOG_I(TAG, "snmp already running"); return; }
    if (lock()) { dev_.reset((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)); unlock(); }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(serverTaskEntry, "snmp_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "snmp agent up on :%u", (unsigned)snmp::UDP_PORT);
}

void SnmpService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    serverTask_ = nullptr;
    LOG_I(TAG, "snmp down");
}

void SnmpService::serverTaskEntry(void* arg) {
    auto* self = static_cast<SnmpService*>(arg);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { LOG_E(TAG, "socket failed"); vTaskDelete(nullptr); return; }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(snmp::UDP_PORT);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_E(TAG, "bind :%u failed", (unsigned)snmp::UDP_PORT);
        close(sock); vTaskDelete(nullptr); return;
    }
    timeval tv = { .tv_sec = 0, .tv_usec = 300000 }; // 300ms: honour the running flag + advance the hold timer
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static uint8_t in[600];
    static uint8_t out[700];
    while (self->running_.load()) {
        const uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        // Advance the device's recovery-verification hold timer regardless of traffic.
        if (self->lock()) { self->dev_.tick(nowMs); self->unlock(); }

        sockaddr_in from = {};
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, in, sizeof(in), 0, (sockaddr*)&from, &flen);
        if (n <= 0) continue; // timeout -> loop (and re-tick)

        self->touched_.store(true);
        size_t rn = 0;
        if (self->lock()) {
            rn = ot::net::snmpwire::process(self->dev_, in, (size_t)n, nowMs, out, sizeof(out));
            self->unlock();
        }
        if (rn > 0) sendto(sock, out, rn, 0, (sockaddr*)&from, flen);
    }
    close(sock);
    vTaskDelete(nullptr);
}

bool SnmpService::solved() const { return dev_.solved(); }
bool SnmpService::healthy() const { return dev_.healthy(); }

} // namespace ot
