// SPDX-License-Identifier: Apache-2.0
#include "OpcUaProto.h"

#include <cstring>

namespace ot::net::opcuaproto {

namespace {
size_t putStr(uint8_t* out, size_t off, size_t outCap, const char* s) {
    size_t slen = s ? std::strlen(s) : 0;
    if (slen > 255) slen = 255;
    if (off + 1 + slen > outCap) return off; // no room; emit nothing more
    out[off++] = (uint8_t)slen;
    std::memcpy(out + off, s, slen);
    return off + slen;
}
} // namespace

size_t processMessage(opcua::OpcUaServer& s, const uint8_t* in, size_t len,
                      uint8_t* out, size_t outCap, size_t& consumed) {
    consumed = 0;
    if (len < 1 || outCap < 2) return 0;
    const uint8_t type = in[0];

    if (type == MSG_HELLO) {
        consumed = 1;
        out[0] = 0x81;
        size_t n = putStr(out, 1, outCap, s.serverName());
        return n;
    }

    if (type == MSG_BROWSE || type == MSG_READ) {
        if (len < 3) return 0; // need the 2-byte NodeId
        consumed = 3;
        const uint16_t nodeId = (uint16_t)((in[1] << 8) | in[2]);

        if (type == MSG_BROWSE) {
            uint16_t ids[opcua::MAX_CHILDREN];
            const char* names[opcua::MAX_CHILDREN];
            int c = s.browse(nodeId, ids, names, opcua::MAX_CHILDREN);
            out[0] = 0x82;
            if (c < 0) { out[1] = 1; out[2] = 0; return 3; }
            out[1] = 0; // ok
            size_t off = 3;
            int emitted = 0;
            for (int i = 0; i < c; i++) {
                size_t slen = names[i] ? std::strlen(names[i]) : 0;
                if (slen > 255) slen = 255;
                if (off + 3 + slen > outCap) break;
                out[off++] = (uint8_t)(ids[i] >> 8);
                out[off++] = (uint8_t)(ids[i] & 0xFF);
                off = putStr(out, off, outCap, names[i]);
                emitted++;
            }
            out[2] = (uint8_t)emitted;
            return off;
        }

        // READ
        const char* val = s.read(nodeId);
        out[0] = 0x83;
        if (val == nullptr) { out[1] = 1; out[2] = 0; return 3; }
        out[1] = 0;
        size_t off = putStr(out, 2, outCap, val);
        return off;
    }

    // Unknown message type: consume one byte and report an error reply.
    consumed = 1;
    out[0] = 0x8F;
    out[1] = 1;
    return 2;
}

} // namespace ot::net::opcuaproto
