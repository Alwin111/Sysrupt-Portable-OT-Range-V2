// SPDX-License-Identifier: Apache-2.0
#include "HistorianService.h"

#include "net/HistProto.h"
#include "net/TcpServer.h"

#include <tactility/log.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "Hist";
constexpr int MAX_CLIENTS = 2;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);

struct HistConnection {
    char buf[128];
    size_t len = 0;
};

class HistHandler : public net::ConnectionHandler {
public:
    explicit HistHandler(HistorianService& svc) : svc_(svc) {}

    void* onOpen() override {
        // Greet on connect so a player immediately sees it is a line protocol.
        return new HistConnection();
    }
    void onClose(void* ctx) override { delete static_cast<HistConnection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<HistConnection*>(ctx);
        if (conn == nullptr) return false;
        svc_.markTouched();
        for (size_t i = 0; i < len; i++) {
            const char c = (char)data[i];
            if (c == '\n') {
                char out[512];
                size_t n = 0;
                if (svc_.lock()) {
                    n = net::histproto::processLine(svc_.system(), conn->buf, conn->len, out, sizeof(out));
                    svc_.unlock();
                }
                conn->len = 0;
                if (n > 0 && send(fd, out, n, 0) < 0) return false;
            } else if (conn->len + 1 < sizeof(conn->buf)) {
                conn->buf[conn->len++] = c;
            } else {
                conn->len = 0; // overlong line: drop it
            }
        }
        return true;
    }

private:
    HistorianService& svc_;
};

} // namespace

HistorianService& HistorianService::instance() {
    static HistorianService service;
    return service;
}

bool HistorianService::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE;
}

void HistorianService::unlock() {
    xSemaphoreGive(mutex_);
}

void HistorianService::start() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (running_.load()) {
        // Already up: do NOT reset (reopening the challenge would wipe a captured flag).
        LOG_I(TAG, "historian already running");
        return;
    }
    if (lock()) {
        sys_.reset();
        unlock();
    }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(serverTaskEntry, "hist_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "historian up on :%u", (unsigned)hist::PORT);
}

void HistorianService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    serverTask_ = nullptr;
    LOG_I(TAG, "historian down");
}

void HistorianService::serverTaskEntry(void* arg) {
    auto* self = static_cast<HistorianService*>(arg);
    HistHandler handler(*self);
    net::serve(hist::PORT, MAX_CLIENTS, handler, self->running_);
    vTaskDelete(nullptr);
}

bool HistorianService::solved() const { return sys_.solved(); }

} // namespace ot
