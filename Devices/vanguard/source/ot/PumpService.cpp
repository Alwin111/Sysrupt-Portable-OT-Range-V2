// SPDX-License-Identifier: Apache-2.0
#include "PumpService.h"

#include "net/PumpProto.h"
#include "net/TcpServer.h"

#include <tactility/log.h>

#include <esp_timer.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "Pump";
constexpr int MAX_CLIENTS = 2;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t TICK_MS = 200;

struct PumpConnection {
    net::pumpproto::ConnState cs;
    uint8_t buf[64];
    size_t len = 0;
};

class PumpHandler : public net::ConnectionHandler {
public:
    explicit PumpHandler(PumpService& svc) : svc_(svc) {}

    void* onOpen() override { return new PumpConnection(); }
    void onClose(void* ctx) override { delete static_cast<PumpConnection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<PumpConnection*>(ctx);
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
            while (conn->len - off >= net::pumpproto::FRAME_LEN) {
                if (conn->buf[off] != net::pumpproto::SYNC0 || conn->buf[off + 1] != net::pumpproto::SYNC1) {
                    return false; // framing error
                }
                uint8_t out[8];
                size_t n = 0;
                if (svc_.lock()) {
                    n = net::pumpproto::processFrame(svc_.system(), svc_.nowMs(),
                                                     conn->buf + off, net::pumpproto::FRAME_LEN, conn->cs, out, sizeof(out));
                    svc_.unlock();
                }
                svc_.markTouched();
                if (n > 0 && send(fd, out, n, 0) < 0) {
                    return false;
                }
                off += net::pumpproto::FRAME_LEN;
            }
            if (off > 0) {
                std::memmove(conn->buf, conn->buf + off, conn->len - off);
                conn->len -= off;
            }
        }
        return true;
    }

private:
    PumpService& svc_;
};

} // namespace

PumpService& PumpService::instance() {
    static PumpService service;
    return service;
}

uint32_t PumpService::nowMs() const {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool PumpService::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE;
}

void PumpService::unlock() {
    xSemaphoreGive(mutex_);
}

void PumpService::start() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (running_.load()) {
        // Already up: do NOT reset. start() is called again on every menu/challenge-screen open, and
        // resetting here would wipe a flag the player has already captured over the network.
        LOG_I(TAG, "pump already running");
        return;
    }
    if (lock()) {
        sys_.reset(nowMs());
        unlock();
    }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(tickTaskEntry, "pump_tick", 3072, this, tskIDLE_PRIORITY + 2, &tickTask_, 1);
    xTaskCreatePinnedToCore(serverTaskEntry, "pump_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "pump control up on :%u", (unsigned)net::pumpproto::PORT);
}

void PumpService::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    tickTask_ = nullptr;
    serverTask_ = nullptr;
    LOG_I(TAG, "pump control down");
}

void PumpService::tickTaskEntry(void* arg) {
    auto* self = static_cast<PumpService*>(arg);
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

void PumpService::serverTaskEntry(void* arg) {
    auto* self = static_cast<PumpService*>(arg);
    PumpHandler handler(*self);
    net::serve(net::pumpproto::PORT, MAX_CLIENTS, handler, self->running_);
    vTaskDelete(nullptr);
}

bool PumpService::solved() const { return sys_.solved(); }
bool PumpService::targetOpen() const { return sys_.targetOpen(); }
bool PumpService::alerted() const { return sys_.alerted(); }

} // namespace ot
