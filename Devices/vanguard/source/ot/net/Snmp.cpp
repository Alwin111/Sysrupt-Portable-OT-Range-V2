// SPDX-License-Identifier: Apache-2.0
#include "Snmp.h"

#include <cstring>

namespace ot::net::snmpwire {

using ot::snmp::SnmpDevice;
using ot::snmp::Value;
using ot::snmp::ValType;
using ot::snmp::SnmpErr;

namespace {

constexpr size_t MAX_OID = 24;
constexpr size_t MAX_VB = 16;

// ---- BER parsing ------------------------------------------------------------------------------
// Reads a tag + length starting at *pos; on success advances *pos to the value and returns true
// with the value length in *len. Rejects indefinite/oversized lengths.
bool readTL(const uint8_t* b, size_t* pos, size_t end, uint8_t* tag, size_t* len) {
    if (*pos + 2 > end) return false;
    *tag = b[*pos];
    size_t p = *pos + 1;
    uint8_t l0 = b[p++];
    size_t n;
    if ((l0 & 0x80) == 0) {
        n = l0;
    } else {
        const int nb = l0 & 0x7F;
        if (nb == 0 || nb > 3 || p + nb > end) return false; // no indefinite / absurd lengths
        n = 0;
        for (int i = 0; i < nb; i++) n = (n << 8) | b[p++];
    }
    if (p + n > end) return false;
    *pos = p;
    *len = n;
    return true;
}

int32_t readInt(const uint8_t* b, size_t pos, size_t len) {
    if (len == 0 || len > 4) return 0;
    int32_t v = (b[pos] & 0x80) ? -1 : 0; // sign-extend
    for (size_t i = 0; i < len; i++) v = (int32_t)((((uint32_t)v) << 8) | b[pos + i]);
    return v;
}

size_t readOid(const uint8_t* b, size_t pos, size_t len, uint32_t* oid, size_t cap) {
    if (len == 0 || cap < 2) return 0;
    size_t n = 0;
    oid[n++] = b[pos] / 40;
    oid[n++] = b[pos] % 40;
    size_t i = 1;
    while (i < len && n < cap) {
        uint32_t v = 0;
        while (i < len) {
            uint8_t c = b[pos + i++];
            v = (v << 7) | (c & 0x7F);
            if ((c & 0x80) == 0) break;
        }
        oid[n++] = v;
    }
    return n;
}

// ---- BER building (bottom-up into caller buffers) ---------------------------------------------
size_t encLen(uint8_t* out, size_t len) {
    if (len < 0x80) { out[0] = (uint8_t)len; return 1; }
    if (len < 0x100) { out[0] = 0x81; out[1] = (uint8_t)len; return 2; }
    out[0] = 0x82; out[1] = (uint8_t)(len >> 8); out[2] = (uint8_t)(len & 0xFF); return 3;
}
// Writes tag + length + content. Returns total bytes, or 0 if it would overflow.
size_t wrap(uint8_t tag, const uint8_t* content, size_t clen, uint8_t* out, size_t cap) {
    uint8_t lb[3]; size_t ln = encLen(lb, clen);
    if (1 + ln + clen > cap) return 0;
    size_t p = 0;
    out[p++] = tag;
    std::memcpy(out + p, lb, ln); p += ln;
    std::memcpy(out + p, content, clen); p += clen;
    return p;
}
size_t encInt(int32_t v, uint8_t* out, size_t cap) {
    uint8_t t[5]; int n = 0;
    // minimal two's-complement encoding
    uint32_t u = (uint32_t)v;
    uint8_t bytes[4] = { (uint8_t)(u >> 24), (uint8_t)(u >> 16), (uint8_t)(u >> 8), (uint8_t)u };
    int start = 0;
    while (start < 3 && ((bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) ||
                         (bytes[start] == 0xFF && (bytes[start + 1] & 0x80) != 0))) start++;
    for (int i = start; i < 4; i++) t[n++] = bytes[i];
    return wrap(0x02, t, (size_t)n, out, cap);
}
size_t encOctet(const char* s, size_t slen, uint8_t* out, size_t cap) {
    return wrap(0x04, (const uint8_t*)s, slen, out, cap);
}
size_t encNull(uint8_t* out, size_t cap) { return wrap(0x05, nullptr, 0, out, cap); }
size_t encOid(const uint32_t* oid, size_t n, uint8_t* out, size_t cap) {
    uint8_t body[64]; size_t p = 0;
    if (n < 2) return 0;
    body[p++] = (uint8_t)(oid[0] * 40 + oid[1]);
    for (size_t i = 2; i < n; i++) {
        uint32_t v = oid[i];
        uint8_t tmp[5]; int tn = 0;
        tmp[tn++] = (uint8_t)(v & 0x7F);
        v >>= 7;
        while (v) { tmp[tn++] = (uint8_t)((v & 0x7F) | 0x80); v >>= 7; }
        for (int k = tn - 1; k >= 0; k--) { if (p >= sizeof(body)) return 0; body[p++] = tmp[k]; }
    }
    return wrap(0x06, body, p, out, cap);
}

// Encodes one device Value (or NULL) as a BER value TLV into out. Returns bytes.
size_t encValue(const Value& v, uint8_t* out, size_t cap) {
    if (v.type == ValType::Integer) return encInt(v.ival, out, cap);
    if (v.type == ValType::OctetString) return encOctet(v.sval, std::strlen(v.sval), out, cap);
    return encNull(out, cap); // NoSuchObject -> NULL
}

struct Vb {
    uint32_t oid[MAX_OID]; size_t oidLen = 0;
    uint8_t valTag = 0x05; size_t valPos = 0; size_t valLen = 0; // raw value slice (for SET)
};

} // namespace

size_t process(SnmpDevice& dev, const uint8_t* in, size_t inLen, uint32_t now_ms,
               uint8_t* out, size_t outCap) {
    size_t pos = 0; uint8_t tag; size_t len;
    // outer SEQUENCE
    if (!readTL(in, &pos, inLen, &tag, &len) || tag != 0x30) return 0;
    const size_t msgEnd = pos + len;
    // version INTEGER
    if (!readTL(in, &pos, msgEnd, &tag, &len) || tag != 0x02) return 0;
    const int32_t version = readInt(in, pos, len); pos += len;
    if (version != 0 && version != 1) return 0; // v1 / v2c only
    // community OCTET STRING
    if (!readTL(in, &pos, msgEnd, &tag, &len) || tag != 0x04) return 0;
    char community[40] = {};
    const size_t clen = len < sizeof(community) - 1 ? len : sizeof(community) - 1;
    std::memcpy(community, in + pos, clen); pos += len;
    // PDU
    if (!readTL(in, &pos, msgEnd, &tag, &len)) return 0;
    const uint8_t pduType = tag;
    if (pduType != PDU_GET && pduType != PDU_GETNEXT && pduType != PDU_SET) return 0;
    const size_t pduEnd = pos + len;
    // request-id, error-status, error-index (all INTEGER)
    if (!readTL(in, &pos, pduEnd, &tag, &len) || tag != 0x02) return 0;
    const int32_t requestId = readInt(in, pos, len); pos += len;
    if (!readTL(in, &pos, pduEnd, &tag, &len) || tag != 0x02) return 0;
    pos += len; // error-status (ignored in a request)
    if (!readTL(in, &pos, pduEnd, &tag, &len) || tag != 0x02) return 0;
    pos += len; // error-index
    // varbind list SEQUENCE
    if (!readTL(in, &pos, pduEnd, &tag, &len) || tag != 0x30) return 0;
    const size_t vblEnd = pos + len;

    // Static scratch: process() is only ever called from the single SNMP server task (and, in tests,
    // single-threaded), so keeping the large BER buffers off the stack is safe and avoids stack
    // pressure on the service task.
    static Vb vbs[MAX_VB]; size_t nvb = 0;
    while (pos < vblEnd && nvb < MAX_VB) {
        if (!readTL(in, &pos, vblEnd, &tag, &len) || tag != 0x30) return 0;
        const size_t vbEnd = pos + len;
        if (!readTL(in, &pos, vbEnd, &tag, &len) || tag != 0x06) return 0; // OID
        vbs[nvb].oidLen = readOid(in, pos, len, vbs[nvb].oid, MAX_OID); pos += len;
        if (!readTL(in, &pos, vbEnd, &tag, &len)) return 0; // value
        vbs[nvb].valTag = tag; vbs[nvb].valPos = pos; vbs[nvb].valLen = len; pos += len;
        pos = vbEnd;
        nvb++;
    }

    // A real agent silently drops a read with the wrong community. SET auth is enforced per-varbind
    // by the device (it returns authorizationError), which we surface in the response.
    if ((pduType == PDU_GET || pduType == PDU_GETNEXT) &&
        std::strcmp(community, ot::snmp::READ_COMMUNITY) != 0) {
        return 0;
    }

    // Build the varbind list, tracking the first error.
    static uint8_t vbl[768]; size_t vblp = 0;
    int errStatus = 0, errIndex = 0;
    for (size_t i = 0; i < nvb; i++) {
        uint32_t respOid[MAX_OID]; size_t respOidLen = vbs[i].oidLen;
        std::memcpy(respOid, vbs[i].oid, respOidLen * sizeof(uint32_t));
        Value val;
        if (pduType == PDU_GET) {
            val = dev.get(vbs[i].oid, vbs[i].oidLen);
            if (val.type == ValType::NoSuchObject && errStatus == 0) { errStatus = 2; errIndex = (int)i + 1; }
        } else if (pduType == PDU_GETNEXT) {
            size_t nl = 0;
            val = dev.getNext(vbs[i].oid, vbs[i].oidLen, respOid, &nl, MAX_OID);
            if (val.type == ValType::NoSuchObject) { if (errStatus == 0) { errStatus = 2; errIndex = (int)i + 1; } }
            else respOidLen = nl;
        } else { // SET
            int32_t sv = (vbs[i].valTag == 0x02) ? readInt(in, vbs[i].valPos, vbs[i].valLen) : 0;
            SnmpErr e = (vbs[i].valTag == 0x02)
                            ? dev.setInteger(vbs[i].oid, vbs[i].oidLen, sv, community, now_ms)
                            : SnmpErr::WrongType;
            if (e != SnmpErr::None && errStatus == 0) { errStatus = (int)e; errIndex = (int)i + 1; }
            val = dev.get(vbs[i].oid, vbs[i].oidLen); // echo the resulting value
        }
        // encode the varbind: SEQUENCE { OID, value }
        static uint8_t inner[512]; size_t ip = 0;
        size_t w = encOid(respOid, respOidLen, inner + ip, sizeof(inner) - ip); if (!w) return 0; ip += w;
        w = encValue(val, inner + ip, sizeof(inner) - ip); if (!w) return 0; ip += w;
        w = wrap(0x30, inner, ip, vbl + vblp, sizeof(vbl) - vblp); if (!w) return 0; vblp += w;
    }

    // RESPONSE PDU: request-id, error-status, error-index, varbindlist
    static uint8_t pdu[900]; size_t pp = 0;
    size_t w;
    w = encInt(requestId, pdu + pp, sizeof(pdu) - pp); if (!w) return 0; pp += w;
    w = encInt(errStatus, pdu + pp, sizeof(pdu) - pp); if (!w) return 0; pp += w;
    w = encInt(errIndex, pdu + pp, sizeof(pdu) - pp); if (!w) return 0; pp += w;
    w = wrap(0x30, vbl, vblp, pdu + pp, sizeof(pdu) - pp); if (!w) return 0; pp += w;

    // message: version, community, RESPONSE pdu
    static uint8_t body[980]; size_t bp = 0;
    w = encInt(version, body + bp, sizeof(body) - bp); if (!w) return 0; bp += w;
    w = encOctet(community, std::strlen(community), body + bp, sizeof(body) - bp); if (!w) return 0; bp += w;
    w = wrap(PDU_RESPONSE, pdu, pp, body + bp, sizeof(body) - bp); if (!w) return 0; bp += w;

    return wrap(0x30, body, bp, out, outCap);
}

} // namespace ot::net::snmpwire
