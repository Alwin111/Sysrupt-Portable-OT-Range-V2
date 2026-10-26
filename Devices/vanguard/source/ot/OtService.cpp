// SPDX-License-Identifier: Apache-2.0
#include "OtService.h"

#include "../BadgeId.h"
#include "net/TcpServer.h"
#include "net/Zones.h"
#include "sim/PlantConfig.h"

#include <tactility/log.h>

#include <esp_timer.h>

#include <lwip/sockets.h>

#include <cstring>

namespace ot {

namespace {

constexpr auto* TAG = "OtService";
constexpr uint16_t MODBUS_PORT = 502;
constexpr int MODBUS_MAX_CLIENTS = 4;
constexpr TickType_t LOCK_TIMEOUT = pdMS_TO_TICKS(100);

// The attacker-facing Wi-Fi AP. The SSID is the per-badge unique identity (see BadgeId): every badge
// broadcasts a different SSID (SYSRUPT-OT-V2-NNN / -XXXX) so players can pick their own badge out of
// many identical ones. The displayed SSID (starting screen) comes from the same source.
constexpr auto* RANGE_PASSWORD = ""; // empty -> open network (WIFI_AUTH_OPEN, no password)

// Per-connection Modbus framing: accumulate bytes until a full ADU is present, process it, reply,
// and keep any trailing bytes for the next request.
struct ModbusConnection {
    uint8_t buf[modbus::MAX_ADU];
    size_t len = 0;
};

class ModbusConnectionHandler : public net::ConnectionHandler {
public:
    explicit ModbusConnectionHandler(OtService& service) : service_(service) {}

    void* onOpen() override { return new ModbusConnection(); }
    void onClose(void* ctx) override { delete static_cast<ModbusConnection*>(ctx); }

    bool onData(int fd, void* ctx, const uint8_t* data, size_t len) override {
        auto* conn = static_cast<ModbusConnection*>(ctx);
        if (conn == nullptr) {
            return false;
        }
        while (len > 0) {
            const size_t space = sizeof(conn->buf) - conn->len;
            if (space == 0) {
                return false; // oversized/garbage: drop the connection
            }
            const size_t take = len < space ? len : space;
            std::memcpy(conn->buf + conn->len, data, take);
            conn->len += take;
            data += take;
            len -= take;

            // Process as many complete ADUs as the buffer now holds.
            while (conn->len >= modbus::MBAP_LEN) {
                const uint16_t mbapLen = (uint16_t)((conn->buf[4] << 8) | conn->buf[5]);
                const size_t aduLen = (size_t)mbapLen + 6;
                if (aduLen > sizeof(conn->buf)) {
                    return false; // malformed length
                }
                if (conn->len < aduLen) {
                    break; // wait for the rest
                }
                // Record attacker activity by function code (ADU byte 7) before serving it.
                if (aduLen > 7) {
                    service_.noteModbus(conn->buf[7]);
                }
                uint8_t resp[modbus::MAX_ADU];
                const size_t n = modbus::process(conn->buf, aduLen, resp, sizeof(resp), service_);
                if (n > 0 && send(fd, resp, n, 0) < 0) {
                    return false;
                }
                // Shift any trailing bytes down.
                std::memmove(conn->buf, conn->buf + aduLen, conn->len - aduLen);
                conn->len -= aduLen;
            }
        }
        return true;
    }

private:
    OtService& service_;
};

} // namespace

OtService& OtService::instance() {
    static OtService service;
    return service;
}

uint32_t OtService::nowMs() const {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool OtService::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, LOCK_TIMEOUT) == pdTRUE;
}

void OtService::unlock() {
    xSemaphoreGive(mutex_);
}

void OtService::start(Challenge challenge) {
    challenge_ = challenge;

    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    if (running_.load()) {
        // Already up: do NOT reseed. start() is called again every time the player opens a reactor
        // challenge screen, and reseeding here would wipe a flag they have already captured. A fresh
        // scenario for a new player is handled by the presence gate (setPlayerPresent), not here.
        LOG_I(TAG, "Reactor already running (challenge %d)", (int)challenge);
        return;
    }

    if (lock()) {
        plant_.reset(nowMs(), challenge);
        unlock();
    }

    // Bring up the attacker-facing Wi-Fi AP (the badge becomes the OT range network). The SSID is this
    // badge's unique identity, so it matches the SSID shown on the starting screen.
    network_.start(badge::ssid(), RANGE_PASSWORD);
    // Bring up the web HMI front door (operator login -> robots.txt -> /control privesc -> HMI).
    http_.start();

    running_.store(true);
    xTaskCreatePinnedToCore(simTaskEntry, "ot_sim", 3072, this, tskIDLE_PRIORITY + 3, &simTask_, 1);
    xTaskCreatePinnedToCore(modbusTaskEntry, "ot_modbus", 4096, this, tskIDLE_PRIORITY + 2, &modbusTask_, 0);

    LOG_I(TAG, "Started challenge %d", (int)challenge);
}

void OtService::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    // The tasks poll running_ and exit on their own; give them time to close sockets first.
    vTaskDelay(pdMS_TO_TICKS(400));
    simTask_ = nullptr;
    modbusTask_ = nullptr;
    http_.stop();
    network_.stop();
    LOG_I(TAG, "Stopped");
}

void OtService::noteModbus(uint8_t function_code) {
    switch (function_code) {
        case 0x03: // read holding registers (actuators/control)
            mappedHolding_.store(true);
            reads_.fetch_add(1);
            break;
        case 0x04: // read input registers (sensors/flags)
            mappedInput_.store(true);
            reads_.fetch_add(1);
            break;
        case 0x06: // write single register
        case 0x10: // write multiple registers
            writes_.fetch_add(1);
            break;
        default:
            break;
    }
}

void OtService::setPlayerPresent(bool present) {
    const bool prev = playerPresent_.exchange(present);
    if (present && !prev) {
        // Fresh attacker: reseed a clean scenario and a new detection session.
        if (lock()) {
            plant_.reset(nowMs(), challenge_);
            unlock();
        }
        mappedInput_.store(false);
        mappedHolding_.store(false);
        reads_.store(0);
        writes_.store(0);
        ch1_ever_.store(false);
        ch2_ever_.store(false);
        sessionId_.fetch_add(1);
        active_.store(true);
        LOG_I(TAG, "Player joined: session %u, reactor armed", (unsigned)sessionId_.load());
    } else if (!present && prev) {
        active_.store(false); // pause physics while idle
        LOG_I(TAG, "Player left: reactor frozen");
    }
}

OtService::Metrics OtService::metrics() const {
    return Metrics {
        sessionId_.load(),
        mappedInput_.load(),
        mappedHolding_.load(),
        reads_.load(),
        writes_.load(),
    };
}

bool OtService::challengeSolved(int number) const {
    if (number == 1) return ch1_ever_.load();
    if (number == 2) return ch2_ever_.load();
    return false;
}

void OtService::resetChallenges() {
    // Reseed the reactor to its clean (deliberately unsafe) start and clear the solved latches, the
    // same state a fresh player join produces (setPlayerPresent) -- but without dropping the AP or
    // the running tasks. Bump the session id so any scoreboard latches reset too.
    if (lock()) {
        plant_.reset(nowMs(), challenge_);
        unlock();
    }
    ch1_ever_.store(false);
    ch2_ever_.store(false);
    sessionId_.fetch_add(1);
}

net::RangeNetworkStatus OtService::networkStatus() {
    return network_.status();
}

const char* OtService::ssid() const {
    return badge::ssid();
}

PlantSnapshot OtService::snapshot() {
    PlantSnapshot s {};
    if (lock()) {
        s = plant_.snapshot();
        unlock();
    }
    return s;
}

uint16_t OtService::readHolding(uint16_t addr) {
    uint16_t v = 0;
    if (lock()) {
        v = plant_.readHolding(addr);
        unlock();
    }
    return v;
}

uint16_t OtService::readInput(uint16_t addr) {
    uint16_t v = 0;
    if (lock()) {
        v = plant_.readInput(addr);
        unlock();
    }
    return v;
}

bool OtService::writeHolding(uint16_t addr, uint16_t value) {
    bool ok = false;
    if (lock()) {
        ok = plant_.writeHolding(addr, value, nowMs());
        unlock();
    }
    return ok;
}

void OtService::simTaskEntry(void* arg) {
    auto* self = static_cast<OtService*>(arg);
    TickType_t last = xTaskGetTickCount();
    // A player is "present" while a Wi-Fi station is associated, or was within this grace window.
    // Phones power-save and briefly disassociate; without this debounce every such blip looks like
    // the player leaving and a new one joining, which reseeds the plant and WIPES a captured flag.
    constexpr uint32_t PRESENCE_GRACE_MS = 45000;
    while (self->running_.load()) {
        // Presence gating is driven here (not from the UI) so it works whatever screen is open:
        // the plant only advances while a Wi-Fi client is connected.
        const uint8_t clients = self->network_.status().clients;
        const uint32_t now = self->nowMs();
        if (clients >= 1) self->lastClientMs_ = now;
        const bool present = clients >= 1 ||
            (self->lastClientMs_ != 0 && (now - self->lastClientMs_) < PRESENCE_GRACE_MS);
        self->setPlayerPresent(present);

        if (self->active_.load() && self->lock()) {
            self->plant_.tick(self->nowMs());
            const PlantSnapshot s = self->plant_.snapshot();
            self->unlock();
            if (s.ch1_solved) self->ch1_ever_.store(true);
            if (s.ch2_solved) self->ch2_ever_.store(true);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(cfg::TICK_MS));
    }
    vTaskDelete(nullptr);
}

void OtService::modbusTaskEntry(void* arg) {
    auto* self = static_cast<OtService*>(arg);
    ModbusConnectionHandler handler(*self);
    net::serve(MODBUS_PORT, MODBUS_MAX_CLIENTS, handler, self->running_);

    vTaskDelete(nullptr);
}

} // namespace ot
