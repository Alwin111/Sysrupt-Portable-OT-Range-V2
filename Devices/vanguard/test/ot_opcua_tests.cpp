// SPDX-License-Identifier: Apache-2.0
// Host tests for the OPC UA Node Discovery model + protocol (Challenge 7).
// Run: g++ -std=c++17 -I../source/ot ot_opcua_tests.cpp ../source/ot/sim/OpcUaServer.cpp \
//        ../source/ot/net/OpcUaProto.cpp && ./a.out
#include "net/OpcUaProto.h"
#include "sim/OpcUaServer.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ot;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

// Helper: run one request through the codec, return the reply bytes.
static std::vector<uint8_t> msg(opcua::OpcUaServer& s, std::vector<uint8_t> req) {
    uint8_t out[300]; size_t consumed = 0;
    size_t n = net::opcuaproto::processMessage(s, req.data(), req.size(), out, sizeof(out), consumed);
    return std::vector<uint8_t>(out, out + n);
}
static std::vector<uint8_t> browse(opcua::OpcUaServer& s, uint16_t id) {
    return msg(s, { net::opcuaproto::MSG_BROWSE, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF) });
}
static std::vector<uint8_t> read(opcua::OpcUaServer& s, uint16_t id) {
    return msg(s, { net::opcuaproto::MSG_READ, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF) });
}

static void testHello() {
    opcua::OpcUaServer s; s.reset();
    auto r = msg(s, { net::opcuaproto::MSG_HELLO });
    CHECK(!r.empty() && r[0] == 0x81);
    std::string name((const char*)r.data() + 2, r.size() - 2);
    CHECK(name == s.serverName());
}

static void testBrowseTree() {
    opcua::OpcUaServer s; s.reset();
    auto root = browse(s, opcua::NODE_ROOT);
    CHECK(root[0] == 0x82 && root[1] == 0 && root[2] == 2); // Objects + Server
    auto objs = browse(s, opcua::NODE_OBJECTS);
    CHECK(objs[0] == 0x82 && objs[1] == 0 && objs[2] == 3); // Reactor/Pump/Grid
    // The hidden flag node is never listed as a child of anything browsable.
    auto server = browse(s, opcua::NODE_SERVER);
    std::string blob((const char*)objs.data(), objs.size());
    blob += std::string((const char*)server.data(), server.size());
    blob += std::string((const char*)root.data(), root.size());
    CHECK(blob.find("HiddenTag") == std::string::npos);
}

static void testBrowseUnknownNode() {
    opcua::OpcUaServer s; s.reset();
    auto r = browse(s, 0x1234);
    CHECK(r[0] == 0x82 && r[1] == 1); // status 1 = not found
}

// READ reply = [0x83, status(1), valLen(1), value...]; value starts at offset 3.
static std::string readValue(const std::vector<uint8_t>& r) {
    if (r.size() < 3) return "";
    size_t vlen = r[2];
    if (3 + vlen > r.size()) vlen = r.size() - 3;
    return std::string((const char*)r.data() + 3, vlen);
}

static void testDiagnosticsLeaksNodeId() {
    opcua::OpcUaServer s; s.reset();
    std::string v = readValue(read(s, opcua::NODE_DIAGNOSTICS));
    CHECK(v.find("0x5000") != std::string::npos); // the breadcrumb
    CHECK(!s.solved());
}

static void testReadHiddenFlagSolves() {
    opcua::OpcUaServer s; s.reset();
    auto r = read(s, opcua::NODE_FLAG);
    CHECK(r[0] == 0x83 && r[1] == 0);
    CHECK(readValue(r) == opcua::OPCUA_FLAG);
    CHECK(s.solved());
}

static void testReadUnknownNode() {
    opcua::OpcUaServer s; s.reset();
    auto r = read(s, 0x4999);
    CHECK(r[0] == 0x83 && r[1] == 1); // not found
    CHECK(!s.solved());
}

static void testPartialMessageWaits() {
    opcua::OpcUaServer s; s.reset();
    uint8_t out[300]; size_t consumed = 999;
    uint8_t partial[] = { net::opcuaproto::MSG_READ, 0x50 }; // missing 1 byte of NodeId
    size_t n = net::opcuaproto::processMessage(s, partial, sizeof(partial), out, sizeof(out), consumed);
    CHECK(n == 0 && consumed == 0);
}

int main() {
    testHello();
    testBrowseTree();
    testBrowseUnknownNode();
    testDiagnosticsLeaksNodeId();
    testReadHiddenFlagSolves();
    testReadUnknownNode();
    testPartialMessageWaits();
    printf("opcua tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
