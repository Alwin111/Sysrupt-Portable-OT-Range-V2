// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/SnmpDevice.h"

#include <cstddef>
#include <cstdint>

// Minimal SNMP v1/v2c wire codec (BER/ASN.1) for Challenge 08. Supports GetRequest, GetNextRequest
// (so an SNMP walk works) and SetRequest over UDP:161, driving an ot::snmp::SnmpDevice. Host-tested.
// Scope is deliberately small: INTEGER / OCTET STRING / OID / NULL values, request IDs up to 4 bytes,
// lengths up to the datagram size. Not a general SNMP agent.
namespace ot::net::snmpwire {

// PDU tags.
constexpr uint8_t PDU_GET = 0xA0;
constexpr uint8_t PDU_GETNEXT = 0xA1;
constexpr uint8_t PDU_RESPONSE = 0xA2;
constexpr uint8_t PDU_SET = 0xA3;

// Parse one SNMP request datagram, apply it to `dev`, and build the response datagram into `out`.
// Returns the response length, or 0 to send nothing (malformed, or an unauthenticated read that a
// real agent would silently drop). `now_ms` is forwarded to the device for SET side effects.
size_t process(ot::snmp::SnmpDevice& dev, const uint8_t* in, size_t inLen, uint32_t now_ms,
               uint8_t* out, size_t outCap);

} // namespace ot::net::snmpwire
