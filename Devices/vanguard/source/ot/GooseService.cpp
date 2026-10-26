// SPDX-License-Identifier: Apache-2.0
#include "GooseService.h"

#include "net/Goose.h"

#include <tactility/log.h>

#include <lwip/sockets.h>

#include <cstdio>
#include <cstring>

namespace ot {

namespace {
constexpr auto* TAG = "Goose";
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t PUBLISH_MS = 1000;      // broadcast cadence of the normal stream
constexpr uint32_t BASELINE_STNUM = 1;     // steady-state stNum (attacker must beat this)
} // namespace

GooseService& GooseService::instance() {
    static GooseService service;
    return service;
}

bool GooseService::lock() { return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE; }
void GooseService::unlock() { xSemaphoreGive(mutex_); }

void GooseService::start() {
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    if (running_.load()) { LOG_I(TAG, "goose already running"); return; }
    if (lock()) {
        sys_.reset();
        // Seed the subscriber baseline so a stale replay of the steady-state frame is rejected.
        goose::Pdu base{ goose::EXPECTED_APPID, BASELINE_STNUM, 0, goose::EXPECTED_CONFREV, false, false };
        sys_.process(base);
        unlock();
    }
    touched_.store(false);
    running_.store(true);
    // One task + one UDP socket does both receive and periodic broadcast, to stay within the LWIP
    // socket budget shared with the 6 TCP challenge services and the :80 HTTP server.
    xTaskCreatePinnedToCore(serverTaskEntry, "goose_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "goose (training UDP) up on :%u", (unsigned)goose::UDP_PORT);
}

void GooseService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    serverTask_ = nullptr;
    LOG_I(TAG, "goose down");
}

// One UDP socket, one task: receive + reply to forged frames, and periodically broadcast the normal
// steady-state stream (breaker closed, fixed stNum, sqNum++) so a connected client can observe the
// APPID + counters to forge against.
void GooseService::serverTaskEntry(void* arg) {
    auto* self = static_cast<GooseService*>(arg);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { LOG_E(TAG, "socket failed"); vTaskDelete(nullptr); return; }
    int bc = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(goose::UDP_PORT);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_E(TAG, "bind :%u failed", (unsigned)goose::UDP_PORT);
        close(sock); vTaskDelete(nullptr); return;
    }
    timeval tv = { .tv_sec = 0, .tv_usec = 300000 }; // 300ms so the running flag + publish cadence are honoured
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in bdst = {};
    bdst.sin_family = AF_INET;
    bdst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    bdst.sin_port = htons(goose::UDP_PORT);

    uint32_t sq = 0;
    uint32_t lastPublishMs = 0;
    while (self->running_.load()) {
        // Broadcast the steady-state frame on the publish cadence.
        uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (nowMs - lastPublishMs >= PUBLISH_MS) {
            lastPublishMs = nowMs;
            goose::Pdu p{ goose::EXPECTED_APPID, BASELINE_STNUM, sq++, goose::EXPECTED_CONFREV, false, false };
            uint8_t frame[ot::net::goose::FRAME_LEN];
            size_t fn = ot::net::goose::encode(p, frame, sizeof(frame));
            sendto(sock, frame, fn, 0, (sockaddr*)&bdst, sizeof(bdst));
        }

        uint8_t buf[64];
        sockaddr_in from = {};
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
        if (n <= 0) continue; // timeout or error -> loop (and re-check publish cadence)

        // Ignore our own broadcast echo (steady-state frame) so it is not treated as a client probe.
        goose::Pdu pdu{};
        char reply[96];
        if (!ot::net::goose::parse(buf, (size_t)n, pdu)) {
            std::snprintf(reply, sizeof(reply), "REJECTED malformed-frame\n");
        } else {
            goose::Result r = goose::Result::RejectedStale;
            bool solved = false;
            if (self->lock()) {
                r = self->sys_.process(pdu);
                solved = self->sys_.solved();
                self->unlock();
            }
            // Only count an inbound frame as player interaction if it is not the steady-state baseline.
            if (!(pdu.stNum == BASELINE_STNUM && !pdu.breakerOpen)) self->touched_.store(true);
            const char* rs =
                r == goose::Result::Accepted ? "ACCEPTED" :
                r == goose::Result::RejectedAppid ? "REJECTED appid-mismatch" :
                r == goose::Result::RejectedConfRev ? "REJECTED confrev-mismatch" :
                r == goose::Result::RejectedTest ? "REJECTED test-frame" : "REJECTED stale-stnum";
            if (solved) {
                std::snprintf(reply, sizeof(reply), "%s breaker=OPEN %s\n", rs, goose::GOOSE_FLAG);
            } else {
                std::snprintf(reply, sizeof(reply), "%s breaker=%s\n", rs,
                              self->sys_.breakerOpen() ? "OPEN" : "closed");
            }
        }
        sendto(sock, reply, std::strlen(reply), 0, (sockaddr*)&from, flen);
    }
    close(sock);
    vTaskDelete(nullptr);
}

bool GooseService::solved() const { return sys_.solved(); }
bool GooseService::breakerOpen() const { return sys_.breakerOpen(); }
uint32_t GooseService::lastStNum() const { return sys_.lastStNum(); }

} // namespace ot
