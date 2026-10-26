// SPDX-License-Identifier: Apache-2.0
#include "OpcUaServer.h"

namespace ot::opcua {

namespace {

struct Node {
    uint16_t id;
    const char* name;
    const char* value;
    uint16_t children[MAX_CHILDREN];
    int childCount;
};

// The address space. NODE_FLAG (0x5000) exists and is readable, but is listed as nobody's child, so
// a Browse starting at Root never reaches it. NODE_DIAGNOSTICS leaks its NodeId.
constexpr Node NODES[] = {
    { NODE_ROOT,        "Root",         "FolderType",            { NODE_OBJECTS, NODE_SERVER }, 2 },
    { NODE_OBJECTS,     "Objects",      "FolderType",            { 0x0101, 0x0102, 0x0103 }, 3 },
    { NODE_SERVER,      "Server",       "ServerType",            { 0x0201, NODE_DIAGNOSTICS }, 2 },
    { 0x0101,           "Reactor",      "temp=295.6C flux=82%",  {}, 0 },
    { 0x0102,           "Pump",         "flow=142.5 valve=68%",  {}, 0 },
    { 0x0103,           "Grid",         "freq=50.00Hz",          {}, 0 },
    { 0x0201,           "ServerStatus", "state=Running up=3600s",{}, 0 },
    { NODE_DIAGNOSTICS, "Diagnostics",  "WARN orphan node i=0x5000 unlinked from Objects", {}, 0 },
    { NODE_FLAG,        "HiddenTag",    OPCUA_FLAG,              {}, 0 },
};
constexpr int NODE_COUNT = (int)(sizeof(NODES) / sizeof(Node));

const Node* find(uint16_t id) {
    for (int i = 0; i < NODE_COUNT; i++) {
        if (NODES[i].id == id) return &NODES[i];
    }
    return nullptr;
}

} // namespace

void OpcUaServer::reset() { revealed_ = false; }

int OpcUaServer::browse(uint16_t nodeId, uint16_t* outIds, const char** outNames, int cap) const {
    const Node* n = find(nodeId);
    if (n == nullptr) return -1;
    int c = 0;
    for (int i = 0; i < n->childCount && c < cap; i++) {
        const Node* ch = find(n->children[i]);
        if (ch == nullptr) continue;
        if (outIds) outIds[c] = ch->id;
        if (outNames) outNames[c] = ch->name;
        c++;
    }
    return c;
}

const char* OpcUaServer::read(uint16_t nodeId) {
    const Node* n = find(nodeId);
    if (n == nullptr) return nullptr;
    if (nodeId == NODE_FLAG) revealed_ = true;
    return n->value;
}

const char* OpcUaServer::nodeName(uint16_t nodeId) const {
    const Node* n = find(nodeId);
    return n ? n->name : nullptr;
}

} // namespace ot::opcua
