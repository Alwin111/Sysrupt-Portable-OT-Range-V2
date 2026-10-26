// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../sim/OpcUaServer.h"

#include <cstddef>
#include <cstdint>

// OPC-UA-lite binary protocol for Challenge 7 (Node Discovery), over TCP port 9005 (moved off OPC UA's
// standard 4840 so it no longer masquerades as a standards-compliant server). A deliberately
// small stand-in for OPC UA's Browse/Read services so it reverses without the full stack. Pure logic;
// OpcUaService wraps it with FreeRTOS + a TCP listener.
//
// Requests (big-endian):
//   0x01 HELLO                      -> 0x81 nameLen(1) name
//   0x02 BROWSE nodeId(2)           -> 0x82 status(1) count(1) [ id(2) nameLen(1) name ]*
//   0x03 READ   nodeId(2)           -> 0x83 status(1) valLen(1) value
//   status: 0 = ok, 1 = node not found.
namespace ot::net::opcuaproto {

constexpr uint8_t MSG_HELLO = 0x01;
constexpr uint8_t MSG_BROWSE = 0x02;
constexpr uint8_t MSG_READ = 0x03;

// Parses one request from the front of `in`. On a complete request, writes the reply to `out`,
// sets `consumed` to the request's byte length and returns the reply length. If `in` does not yet
// hold a whole request, sets consumed=0 and returns 0 (caller waits for more bytes).
size_t processMessage(opcua::OpcUaServer& s, const uint8_t* in, size_t len,
                      uint8_t* out, size_t outCap, size_t& consumed);

} // namespace ot::net::opcuaproto
