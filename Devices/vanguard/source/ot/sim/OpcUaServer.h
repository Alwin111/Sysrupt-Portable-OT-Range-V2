// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 7 - OPC UA Node Discovery. Pure logic (host-tested); the OPC-UA-lite protocol
// (net/OpcUaProto) is a thin view onto this address space.
//
// Scenario: an OPC UA server exposes an address space you can BROWSE from the Root (Objects + Server
// folders) and READ node values. One node - the flag - was UNLINKED from the browsable tree
// (orphaned), so a normal Browse never lists it. A diagnostics node leaks its NodeId; the attacker
// must discover the orphan (from the hint, or by sweeping NodeIds) and READ it directly.
//
// Solve = the player READs the hidden flag node (NodeId 0x5000); the challenge then latches solved.
namespace ot::opcua {

// Deliberately NOT OPC UA's standard 4840: this is a self-rolled "OPC-UA-lite" service, so it is moved
// onto a non-standard port and no longer masquerades as a standards-compliant OPC UA server on the
// default port (a standard client could never complete its handshake here). See the correction
// write-up: keep the browse->diagnostics->read puzzle, remove the "looks like real OPC UA" dead end.
constexpr uint16_t PORT = 9005; // self-rolled OPC-UA-lite service (not OPC UA's 4840)

constexpr const char* OPCUA_FLAG = "flag{0pcu4_h1dd3n_n0d3_r3ad}";
constexpr size_t opc_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(opc_cstrlen(OPCUA_FLAG) <= 32, "OPCUA_FLAG must fit 32 bytes");

constexpr uint16_t NODE_ROOT = 0x0000;
constexpr uint16_t NODE_OBJECTS = 0x0100;
constexpr uint16_t NODE_SERVER = 0x0200;
constexpr uint16_t NODE_DIAGNOSTICS = 0x0202;
constexpr uint16_t NODE_FLAG = 0x5000; // orphaned: readable but not a child of any browsable node

constexpr int MAX_CHILDREN = 4;

class OpcUaServer {
public:
    void reset();

    const char* serverName() const { return "Sysrupt-OPCUA/1.0"; }

    // Browse children of `nodeId`. Fills up to `cap` entries; returns child count, or -1 if the node
    // does not exist. A leaf returns 0.
    int browse(uint16_t nodeId, uint16_t* outIds, const char** outNames, int cap) const;

    // Read a node's value string. Returns nullptr if the NodeId does not exist. Reading the hidden
    // flag node latches the challenge solved.
    const char* read(uint16_t nodeId);

    const char* nodeName(uint16_t nodeId) const; // nullptr if unknown

    bool solved() const { return revealed_; }

private:
    bool revealed_ = false;
};

} // namespace ot::opcua
