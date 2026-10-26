// SPDX-License-Identifier: Apache-2.0
#include "Iec104Service.h"

#include "net/Iec104.h"
#include "net/TcpServer.h"

#include <tactility/log.h>

#include <esp_timer.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "Iec104";
constexpr int MAX_CLIENTS = 2;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t TICK_MS = 200;

// Per-connection IEC-104 state: sequence counters plus an accumulation buffer for partial APDUs.
struct Iec104Connection {
    net::iec104::ConnState cs;
    uint8_t buf[net::iec104::MAX_APDU];
    size_t len = 0;
};

class Iec104Handler : public net::ConnectionHandler {
public:
    explicit Iec104Handler(Iec104Service& svc) : svc_(svc) {}

    void* onOpen() override { return new Iec104Connection(); }
    void onClose(void* ctx) override { delete static_cast<Iec104Connection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<Iec104Connection*>(ctx);
        if (conn == nullptr) {
            return false;
        }
        while (len > 0) {
            const size_t space = sizeof(conn->buf) - conn->len;
            if (space == 0) {
                return false; // garbage
            }
            const size_t take = len < space ? len : space;
            std::memcpy(conn->buf + conn->len, data, take);
            conn->len += take;
            data += take;
            len -= take;

            // Process every complete APDU (0x68, length byte, then `length` bytes).
            size_t off = 0;
            while (conn->len - off >= 2) {
                if (conn->buf[off] != 0x68) {
                    return false; // framing error
                }
                const size_t apduLen = (size_t)conn->buf[off + 1] + 2;
                if (conn->len - off < apduLen) {
                    break; // wait for the rest
                }
                uint8_t out[1024];
                size_t n = 0;
                if (svc_.lock()) {
                    n = net::iec104::processApdu(svc_.substation(), svc_.nowMs(),
                                                 conn->buf + off, apduLen, conn->cs, out, sizeof(out));
                    svc_.unlock();
                }
                svc_.markTouched();
                if (n > 0 && send(fd, out, n, 0) < 0) {
                    return false;
                }
                off += apduLen;
            }
            if (off > 0) {
                std::memmove(conn->buf, conn->buf + off, conn->len - off);
                conn->len -= off;
            }
        }
        return true;
    }

private:
    Iec104Service& svc_;
};

} // namespace

Iec104Service& Iec104Service::instance() {
    static Iec104Service service;
    return service;
}

uint32_t Iec104Service::nowMs() const {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool Iec104Service::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE;
}

void Iec104Service::unlock() {
    xSemaphoreGive(mutex_);
}

void Iec104Service::start() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (running_.load()) {
        // Already up: do NOT reset (reopening the challenge would wipe a captured flag).
        LOG_I(TAG, "IEC-104 substation already running");
        return;
    }
    if (lock()) {
        sub_.reset(nowMs());
        unlock();
    }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(tickTaskEntry, "iec_tick", 3072, this, tskIDLE_PRIORITY + 2, &tickTask_, 1);
    xTaskCreatePinnedToCore(serverTaskEntry, "iec_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "IEC-104 substation up on :%u", (unsigned)net::iec104::PORT);
}

void Iec104Service::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    tickTask_ = nullptr;
    serverTask_ = nullptr;
    LOG_I(TAG, "IEC-104 substation down");
}

void Iec104Service::tickTaskEntry(void* arg) {
    auto* self = static_cast<Iec104Service*>(arg);
    TickType_t last = xTaskGetTickCount();
    while (self->running_.load()) {
        if (self->lock()) {
            self->sub_.tick(self->nowMs());
            self->unlock();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
    }
    vTaskDelete(nullptr);
}

void Iec104Service::serverTaskEntry(void* arg) {
    auto* self = static_cast<Iec104Service*>(arg);
    Iec104Handler handler(*self);
    net::serve(net::iec104::PORT, MAX_CLIENTS, handler, self->running_);
    vTaskDelete(nullptr);
}

bool Iec104Service::solved() const {
    return sub_.solved();
}

bool Iec104Service::breakerOpen() const {
    return sub_.breakerOpen();
}

bool Iec104Service::interrogationLock() const {
    return sub_.interrogationLock();
}

} // namespace ot
