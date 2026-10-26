// SPDX-License-Identifier: Apache-2.0
#include "DamService.h"

#include "net/Modem.h"
#include "net/TcpServer.h"

#include <tactility/log.h>

#include <esp_timer.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "Dam";
constexpr int MAX_CLIENTS = 2;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t TICK_MS = 200;

struct DamConnection {
    net::modem::Session session;
    char buf[128];
    size_t len = 0;
    bool greeted = false;
};

class DamHandler : public net::ConnectionHandler {
public:
    explicit DamHandler(DamService& svc) : svc_(svc) {
        bannerLen_ = net::modem::banner(banner_, sizeof(banner_));
    }

    void* onOpen() override { return new DamConnection(); }
    void onClose(void* ctx) override { delete static_cast<DamConnection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<DamConnection*>(ctx);
        if (conn == nullptr) return false;
        if (!conn->greeted) {
            conn->greeted = true;
            if (send(fd, banner_, bannerLen_, 0) < 0) return false;
        }
        while (len > 0) {
            size_t space = sizeof(conn->buf) - conn->len;
            if (space == 0) { conn->len = 0; space = sizeof(conn->buf); } // overflow: drop junk
            const size_t take = len < space ? len : space;
            std::memcpy(conn->buf + conn->len, data, take);
            conn->len += take;
            data += take;
            len -= take;

            // Process each complete line (terminated by CR or LF).
            size_t start = 0;
            for (size_t i = 0; i < conn->len; i++) {
                if (conn->buf[i] == '\n' || conn->buf[i] == '\r') {
                    char out[160];
                    size_t n = 0;
                    if (svc_.lock()) {
                        n = net::modem::process(conn->session, svc_.system(), conn->buf + start,
                                                i - start, svc_.nowMs(), out, sizeof(out));
                        svc_.unlock();
                    }
                    svc_.markTouched();
                    if (n > 0 && send(fd, out, n, 0) < 0) return false;
                    start = i + 1;
                }
            }
            if (start > 0) {
                std::memmove(conn->buf, conn->buf + start, conn->len - start);
                conn->len -= start;
            }
        }
        return true;
    }

private:
    DamService& svc_;
    char banner_[160] = {};
    size_t bannerLen_ = 0;
};

} // namespace

DamService& DamService::instance() {
    static DamService service;
    return service;
}

uint32_t DamService::nowMs() const { return (uint32_t)(esp_timer_get_time() / 1000); }

bool DamService::lock() { return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE; }
void DamService::unlock() { xSemaphoreGive(mutex_); }

void DamService::start() {
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    if (running_.load()) { LOG_I(TAG, "dam already running"); return; }
    if (lock()) { sys_.reset(nowMs()); unlock(); }
    touched_.store(false);
    running_.store(true);
    xTaskCreatePinnedToCore(tickTaskEntry, "dam_tick", 3072, this, tskIDLE_PRIORITY + 2, &tickTask_, 1);
    xTaskCreatePinnedToCore(serverTaskEntry, "dam_srv", 4096, this, tskIDLE_PRIORITY + 2, &serverTask_, 0);
    LOG_I(TAG, "dam modem up on :%u", (unsigned)net::modem::PORT);
}

void DamService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    tickTask_ = nullptr;
    serverTask_ = nullptr;
    LOG_I(TAG, "dam modem down");
}

void DamService::tickTaskEntry(void* arg) {
    auto* self = static_cast<DamService*>(arg);
    TickType_t last = xTaskGetTickCount();
    while (self->running_.load()) {
        if (self->lock()) { self->sys_.tick(self->nowMs()); self->unlock(); }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
    }
    vTaskDelete(nullptr);
}

void DamService::serverTaskEntry(void* arg) {
    auto* self = static_cast<DamService*>(arg);
    DamHandler handler(*self);
    net::serve(net::modem::PORT, MAX_CLIENTS, handler, self->running_);
    vTaskDelete(nullptr);
}

bool DamService::solved() const { return sys_.solved(); }
uint8_t DamService::gate() const { return sys_.gate(); }
bool DamService::atTarget() const { return sys_.atTarget(); }

} // namespace ot
