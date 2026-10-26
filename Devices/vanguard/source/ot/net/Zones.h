// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// The emulated IEC 62443 / Purdue zones, mirroring the Sysrupt OT Range addressing. The badge is
// a Wi-Fi Access Point: the attacker is DHCP'd into the CORP zone and must pivot inward. Deeper
// zones are off-link from the attacker, so their packets arrive at the badge (the gateway) and are
// delivered locally by NAPT DNAT port-maps (see RangeNetwork). Reachability per zone is gated by
// the firmware firewall as challenges are solved.
namespace ot::zone {

enum Id : uint8_t {
    CORP = 1,    // 10.0.1.0/24 - attacker entry (enterprise)
    DMZ = 2,     // 10.0.2.0/24 - historian, jump host, OPC-UA
    SCADA = 3,   // 10.0.3.0/24 - HMI, engineering WS
    PROCESS = 4, // 10.0.4.0/24 - the reactor PLCs (Modbus)
    SAFETY = 5,  // 10.0.5.0/24 - the safety instrumented system (S7comm)
    COUNT = 6
};

// 10.0.<zone>.0/24
constexpr uint32_t subnet(uint8_t z) { return (10u << 24) | ((uint32_t)z << 8); }
// The badge is .1 (the gateway) in every zone.
constexpr uint32_t gateway(uint8_t z) { return subnet(z) | 1u; }
// A host address in a zone.
constexpr uint32_t host(uint8_t z, uint8_t h) { return subnet(z) | h; }

// The AP (corp) gateway the attacker uses; also where every service really lives.
constexpr uint32_t AP_GATEWAY = gateway(CORP); // 10.0.1.1
constexpr uint8_t AP_DHCP_FIRST_OCTET = 100;   // attacker leases start at 10.0.1.100

} // namespace ot::zone
