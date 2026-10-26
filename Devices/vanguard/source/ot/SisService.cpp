// SPDX-License-Identifier: Apache-2.0
#include "SisService.h"

#include "net/TcpServer.h"
#include "net/Tristation.h"

#include <tactility/log.h>

#include <esp_timer.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "SIS";
constexpr int MAX_CLIENTS = 2;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t TICK_MS = 200;

struct SisConnection {
    uint8_t buf[64];
    size_t len = 0;
};

class SisHandler : public net::ConnectionHandler {
public:
    explicit SisHandler(SisService& svc) : svc_(svc) {}

    void* onOpen() override { return new SisConnection(); }
    void onClose(void* ctx) override { delete static_cast<SisConnection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<SisConnection*>(ctx);
        if (conn == nullptr) {
            return false;
        }
        while (len > 0) {
            const size_t space = sizeof(conn->buf) - conn->len;
            if (space == 0) {
                return false;
            }
            const size_t take = len < space ? len : space;
            std::memcpy(conn->buf + conn->len, data, take);
            conn->len += take;
            data += take;
            len -= take;

            size_t off = 0;
            while (conn->len - off >= 3) {
                if (conn->buf[off] != net::tristation::SYNC0 || conn->buf[off + 1] != net::tristation::SYNC1) {
                    return false; // framing error
                }
                const size_t frameLen = net::tristation::requestLen(conn->buf[off + 2]);
                if (frameLen == 0) {
                    return false; // unknown command
                }
                if (conn->len - off < frameLen) {
                    break; // wait for the rest
                }
                uint8_t out[16];
                size_t n = 0;
                if (svc_.lock()) {
                    n = net::tristation::processFrame(svc_.system(), svc_.nowMs(),
                                                      conn->buf + off, frameLen, out, sizeof(out));
                    svc_.unlock();
                }
                svc_.markTouched();
                if (n > 0 && send(fd, out, n, 0) < 0) {
                    return false;
                }
                off += frameLen;
            }
            if (off > 0) {
                std::memmove(conn->buf, conn->buf + off, conn->len - off);
                conn->len -= off;
            }
        }
        return true;
    }

private:
    SisService& svc_;
};

} // namespace

SisService& SisService::instance() {
    static SisService service;
    return service;
}

uint32_t SisService::nowMs() const {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool SisService::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE;
}

void SisService::unlock() {
    xSemaphoreGive(mutex_);
}

void SisService::start() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (running_.load()) {
        // Already up: do NOT reset (reopening the challenge would wipe a captured flag).
        LOG_I(TAG, "SIS already running");
        return;
    }
    if (lock()) {
        sys_.reset(nowMs());
        unlock();
    }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(tickTaskEntry, "sis_tick", 3072, this, tskIDLE_PRIORITY + 2, &tickTask_, 1);
    xTaskCreatePinnedToCore(serverTaskEntry, "sis_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "SIS up on :%u", (unsigned)net::tristation::PORT);
}

void SisService::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    tickTask_ = nullptr;
    serverTask_ = nullptr;
    LOG_I(TAG, "SIS down");
}

void SisService::tickTaskEntry(void* arg) {
    auto* self = static_cast<SisService*>(arg);
    TickType_t last = xTaskGetTickCount();
    while (self->running_.load()) {
        if (self->lock()) {
            self->sys_.tick(self->nowMs());
            self->unlock();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
    }
    vTaskDelete(nullptr);
}

void SisService::serverTaskEntry(void* arg) {
    auto* self = static_cast<SisService*>(arg);
    SisHandler handler(*self);
    net::serve(net::tristation::PORT, MAX_CLIENTS, handler, self->running_);
    vTaskDelete(nullptr);
}

bool SisService::solved() const { return sys_.solved(); }
bool SisService::tripEnabled() const { return sys_.tripEnabled(); }
bool SisService::checksumMatch() const { return sys_.checksumMatch(); }

} // namespace ot
