// SPDX-License-Identifier: Apache-2.0
#include "RangeNetwork.h"
#include "Zones.h"

#include <tactility/log.h>

#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include <lwip/sockets.h> // lwip_htonl

#include <cstring>

// Tactility's reference-counted Wi-Fi radio ownership (platform-esp32/source/drivers/esp32_wifi.cpp).
extern "C" bool esp32_wifi_radio_acquire(wifi_mode_t mode_bit);
extern "C" void esp32_wifi_radio_release(wifi_mode_t mode_bit);

namespace ot::net {

namespace {

constexpr auto* TAG = "RangeNet";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CLIENTS = 4;

void set_ip4(esp_ip4_addr_t& out, uint32_t host_order) {
    // esp_ip4_addr_t stores the address in network byte order.
    out.addr = lwip_htonl(host_order);
}

} // namespace

bool RangeNetwork::start(const char* ssid, const char* password) {
    if (running_) {
        return true;
    }

    // Add the AP role to the radio (coexists with Tactility's station via the refcount).
    if (!esp32_wifi_radio_acquire(WIFI_MODE_AP)) {
        LOG_E(TAG, "Failed to acquire AP radio");
        return false;
    }

    auto* ap = static_cast<esp_netif_t*>(ap_netif_);
    if (ap == nullptr) {
        ap = esp_netif_create_default_wifi_ap();
        if (ap == nullptr) {
            LOG_E(TAG, "Failed to create AP netif");
            esp32_wifi_radio_release(WIFI_MODE_AP);
            return false;
        }
        ap_netif_ = ap;
    }

    // Corp subnet: the badge is 10.0.1.1/24 and hands the attacker a 10.0.1.100+ lease.
    esp_netif_dhcps_stop(ap);
    esp_netif_ip_info_t info = {};
    set_ip4(info.ip, zone::AP_GATEWAY);
    set_ip4(info.gw, zone::AP_GATEWAY);
    set_ip4(info.netmask, 0xFFFFFF00u); // 255.255.255.0
    if (esp_netif_set_ip_info(ap, &info) != ESP_OK) {
        LOG_E(TAG, "Failed to set AP IP info");
    }
    esp_netif_dhcps_start(ap);

    // AP configuration.
    wifi_config_t cfg = {};
    std::strncpy((char*)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid_len = (uint8_t)std::strlen(ssid);
    cfg.ap.channel = AP_CHANNEL;
    cfg.ap.max_connection = AP_MAX_CLIENTS;
    if (password != nullptr && password[0] != '\0') {
        std::strncpy((char*)cfg.ap.password, password, sizeof(cfg.ap.password) - 1);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    if (esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK) {
        LOG_E(TAG, "Failed to set AP config");
        esp32_wifi_radio_release(WIFI_MODE_AP);
        return false;
    }

    running_ = true;
    LOG_I(TAG, "OT range AP up: SSID=%s, badge=10.0.1.1", ssid);
    return true;
}

void RangeNetwork::stop() {
    if (!running_) {
        return;
    }
    auto* ap = static_cast<esp_netif_t*>(ap_netif_);
    if (ap != nullptr) {
        esp_netif_dhcps_stop(ap);
    }
    esp32_wifi_radio_release(WIFI_MODE_AP);
    running_ = false;
    LOG_I(TAG, "OT range AP down");
}

RangeNetworkStatus RangeNetwork::status() const {
    RangeNetworkStatus s = {};
    s.ap_up = running_;
    s.ap_ip = zone::AP_GATEWAY;
    wifi_sta_list_t stations = {};
    if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) {
        s.clients = (uint8_t)stations.num;
    }
    return s;
}

} // namespace ot::net
