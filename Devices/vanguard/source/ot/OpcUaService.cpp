// SPDX-License-Identifier: Apache-2.0
#include "OpcUaService.h"

#include "net/OpcUaProto.h"
#include "net/TcpServer.h"

#include <tactility/log.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "OpcUa";
constexpr int MAX_CLIENTS = 2;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);

struct OpcConnection {
    uint8_t buf[64];
    size_t len = 0;
};

class OpcHandler : public net::ConnectionHandler {
public:
    explicit OpcHandler(OpcUaService& svc) : svc_(svc) {}

    void* onOpen() override { return new OpcConnection(); }
    void onClose(void* ctx) override { delete static_cast<OpcConnection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<OpcConnection*>(ctx);
        if (conn == nullptr) return false;
        svc_.markTouched();
        while (len > 0) {
            const size_t space = sizeof(conn->buf) - conn->len;
            if (space == 0) { conn->len = 0; } // overlong: resync
            const size_t take = len < space ? len : space;
            std::memcpy(conn->buf + conn->len, data, take);
            conn->len += take;
            data += take;
            len -= take;

            size_t off = 0;
            while (off < conn->len) {
                uint8_t out[300];
                size_t consumed = 0;
                size_t n = 0;
                if (svc_.lock()) {
                    n = net::opcuaproto::processMessage(svc_.system(), conn->buf + off, conn->len - off,
                                                        out, sizeof(out), consumed);
                    svc_.unlock();
                }
                if (consumed == 0) break; // need more bytes for a full request
                off += consumed;
                if (n > 0 && send(fd, out, n, 0) < 0) return false;
            }
            if (off > 0) {
                std::memmove(conn->buf, conn->buf + off, conn->len - off);
                conn->len -= off;
            }
        }
        return true;
    }

private:
    OpcUaService& svc_;
};

} // namespace

OpcUaService& OpcUaService::instance() {
    static OpcUaService service;
    return service;
}

bool OpcUaService::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE;
}

void OpcUaService::unlock() {
    xSemaphoreGive(mutex_);
}

void OpcUaService::start() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (running_.load()) {
        // Already up: do NOT reset (reopening the challenge would wipe a captured flag).
        LOG_I(TAG, "opcua already running");
        return;
    }
    if (lock()) {
        sys_.reset();
        unlock();
    }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(serverTaskEntry, "opcua_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "opcua up on :%u", (unsigned)opcua::PORT);
}

void OpcUaService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    serverTask_ = nullptr;
    LOG_I(TAG, "opcua down");
}

void OpcUaService::serverTaskEntry(void* arg) {
    auto* self = static_cast<OpcUaService*>(arg);
    OpcHandler handler(*self);
    net::serve(opcua::PORT, MAX_CLIENTS, handler, self->running_);
    vTaskDelete(nullptr);
}

bool OpcUaService::solved() const { return sys_.solved(); }

} // namespace ot
