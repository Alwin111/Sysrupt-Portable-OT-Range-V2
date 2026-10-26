// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// Brings up the OT range's Wi-Fi: the badge becomes a SoftAP that the attacker joins (replacing
// Sysrupt's Ethernet entry). The attacker is DHCP'd into the CORP zone (10.0.1.0/24) with the
// badge as the gateway at 10.0.1.1 - which is also the single host that runs every service.
//
// Zone addressing note: esp-lwip supports one IP per interface and its NAPT port-map forwards to a
// *different* host rather than delivering to the router's own socket, so distinct per-zone host
// IPs (10.0.2.x .. 10.0.5.x) that nmap would see as separate machines are not feasible on this
// MCU. The five Purdue zones are therefore emulated at the application layer: services live on
// 10.0.1.1 at zone-specific ports, and the firewall (Firewall.*) gates which ports accept
// connections as the pivot progresses, so new ports appear as the attacker advances.
//
// Wi-Fi ownership: coexists with Tactility's station radio through its reference-counted
// esp32_wifi_radio_acquire/release(WIFI_MODE_AP), so starting the range adds an AP without
// tearing down the rest of the system.
namespace ot::net {

struct RangeNetworkStatus {
    bool ap_up;
    uint8_t clients;   // associated stations
    uint32_t ap_ip;    // badge AP IP (host byte order)
};

class RangeNetwork {
public:
    // ssid/password for the attacker-facing AP. Returns false if the AP could not be started.
    bool start(const char* ssid, const char* password);
    void stop();
    bool running() const { return running_; }

    RangeNetworkStatus status() const;

private:
    bool running_ = false;
    void* ap_netif_ = nullptr; // esp_netif_t*
};

} // namespace ot::net
