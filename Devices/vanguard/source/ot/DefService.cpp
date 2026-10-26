// SPDX-License-Identifier: Apache-2.0
#include "DefService.h"

#include <tactility/log.h>

#include <esp_timer.h>

namespace ot {

namespace {
constexpr auto* TAG = "Def";
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t TICK_MS = 250;
} // namespace

DefService& DefService::instance() {
    static DefService service;
    return service;
}

uint32_t DefService::nowMs() const { return (uint32_t)(esp_timer_get_time() / 1000); }

bool DefService::lock() { return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE; }
void DefService::unlock() { xSemaphoreGive(mutex_); }

void DefService::start() {
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    if (running_.load()) { LOG_I(TAG, "defensive already running"); return; }
    if (lock()) {
        forensics_.reset();
        trojan_.reset(nowMs());
        takeover_.reset();
        unlock();
    }
    running_.store(true);
    xTaskCreatePinnedToCore(tickTaskEntry, "def_tick", 3072, this, tskIDLE_PRIORITY + 2, &tickTask_, 1);
    LOG_I(TAG, "defensive investigation state up (web :80 /intranet)");
}

void DefService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    tickTask_ = nullptr;
    LOG_I(TAG, "defensive investigation state down");
}

void DefService::tickTaskEntry(void* arg) {
    auto* self = static_cast<DefService*>(arg);
    TickType_t last = xTaskGetTickCount();
    while (self->running_.load()) {
        if (self->lock()) { self->trojan_.tick(self->nowMs()); self->unlock(); }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
    }
    vTaskDelete(nullptr);
}

} // namespace ot
