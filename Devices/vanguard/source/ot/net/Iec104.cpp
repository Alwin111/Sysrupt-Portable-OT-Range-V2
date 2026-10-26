// SPDX-License-Identifier: Apache-2.0
#include "Iec104.h"

#include <cstring>

namespace ot::net::iec104 {

namespace {

// U-frame control functions (first control octet).
constexpr uint8_t U_STARTDT_ACT = 0x07;
constexpr uint8_t U_STARTDT_CON = 0x0B;
constexpr uint8_t U_STOPDT_ACT = 0x13;
constexpr uint8_t U_STOPDT_CON = 0x23;
constexpr uint8_t U_TESTFR_ACT = 0x43;
constexpr uint8_t U_TESTFR_CON = 0x83;

// ASDU type ids
constexpr uint8_t M_SP_NA = 1;
constexpr uint8_t M_ME_NB = 11;
constexpr uint8_t C_SC_NA = 45;
constexpr uint8_t C_IC_NA = 100;

// Causes of transmission
constexpr uint8_t COT_SPONT = 3;
constexpr uint8_t COT_ACT = 6;
constexpr uint8_t COT_ACTCON = 7;
constexpr uint8_t COT_ACTTERM = 10;
constexpr uint8_t COT_INTROGEN = 20;

struct Writer {
    uint8_t* buf;
    size_t cap;
    size_t len = 0;
    bool put(const uint8_t* d, size_t n) {
        if (len + n > cap) return false;
        std::memcpy(buf + len, d, n);
        len += n;
        return true;
    }
};

// Builds one single-object ASDU into `dst` (>= 16 bytes). Returns ASDU length.
size_t buildAsdu(uint8_t* dst, uint8_t type, uint8_t cot, bool negative, uint8_t org, uint16_t ca,
                 uint32_t ioa, const uint8_t* ie, size_t ieLen) {
    size_t n = 0;
    dst[n++] = type;
    dst[n++] = 0x01; // VSQ: SQ=0, 1 information object
    dst[n++] = (uint8_t)((cot & 0x3F) | (negative ? 0x40 : 0));
    dst[n++] = org;
    dst[n++] = (uint8_t)(ca & 0xFF);
    dst[n++] = (uint8_t)(ca >> 8);
    dst[n++] = (uint8_t)(ioa & 0xFF);
    dst[n++] = (uint8_t)((ioa >> 8) & 0xFF);
    dst[n++] = (uint8_t)((ioa >> 16) & 0xFF);
    std::memcpy(dst + n, ie, ieLen);
    n += ieLen;
    return n;
}

// Frames an ASDU as an I-frame APDU and appends it to the writer, advancing N(S).
bool sendI(Writer& w, ConnState& cs, const uint8_t* asdu, size_t asduLen) {
    uint8_t apci[6];
    apci[0] = 0x68;
    apci[1] = (uint8_t)(4 + asduLen);
    apci[2] = (uint8_t)((cs.ns << 1) & 0xFE); // bit0 = 0 -> I-frame
    apci[3] = (uint8_t)((cs.ns >> 7) & 0xFF);
    apci[4] = (uint8_t)((cs.nr << 1) & 0xFE);
    apci[5] = (uint8_t)((cs.nr >> 7) & 0xFF);
    if (!w.put(apci, 6) || !w.put(asdu, asduLen)) return false;
    cs.ns = (uint16_t)((cs.ns + 1) & 0x7FFF);
    return true;
}

// Appends a 6-byte U-frame.
bool sendU(Writer& w, uint8_t func) {
    const uint8_t apci[6] = { 0x68, 0x04, func, 0x00, 0x00, 0x00 };
    return w.put(apci, 6);
}

// Appends an S-frame acknowledging up to N(R).
bool sendS(Writer& w, ConnState& cs) {
    const uint8_t apci[6] = {
        0x68, 0x04, 0x01, 0x00,
        (uint8_t)((cs.nr << 1) & 0xFE), (uint8_t)((cs.nr >> 7) & 0xFF)
    };
    return w.put(apci, 6);
}

// Emits one point as an interrogation (or spontaneous) I-frame.
void sendPoint(Writer& w, ConnState& cs, uint8_t org, uint16_t ca, const sub::Point& p, uint8_t cot) {
    uint8_t asdu[20];
    if (p.type == M_SP_NA) {
        uint8_t ie[1] = { (uint8_t)(p.value ? 1 : 0) };
        size_t n = buildAsdu(asdu, M_SP_NA, cot, false, org, ca, p.ioa, ie, 1);
        sendI(w, cs, asdu, n);
    } else { // M_ME_NB: 16-bit scaled value + QDS
        int16_t v = (int16_t)p.value;
        uint8_t ie[3] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), 0x00 };
        size_t n = buildAsdu(asdu, M_ME_NB, cot, false, org, ca, p.ioa, ie, 3);
        sendI(w, cs, asdu, n);
    }
}

} // namespace

size_t processApdu(sub::Substation& s, uint32_t now_ms,
                   const uint8_t* apdu, size_t len,
                   ConnState& cs, uint8_t* out, size_t outCap) {
    Writer w { out, outCap };
    if (len < 6 || apdu[0] != 0x68) {
        return 0;
    }
    const uint8_t c1 = apdu[2];

    // U-frame
    if ((c1 & 0x03) == 0x03) {
        if (c1 == U_STARTDT_ACT) sendU(w, U_STARTDT_CON);
        else if (c1 == U_STOPDT_ACT) sendU(w, U_STOPDT_CON);
        else if (c1 == U_TESTFR_ACT) sendU(w, U_TESTFR_CON);
        return w.len;
    }
    // S-frame: nothing to do (acknowledgement)
    if ((c1 & 0x03) == 0x01) {
        return 0;
    }

    // I-frame: count it and parse the ASDU.
    cs.nr = (uint16_t)((cs.nr + 1) & 0x7FFF);
    const uint8_t* asdu = apdu + 6;
    const size_t asduLen = len - 6;
    if (asduLen < 10) {
        sendS(w, cs);
        return w.len;
    }
    const uint8_t type = asdu[0];
    const uint8_t cot = asdu[2] & 0x3F;
    const uint8_t org = asdu[3];
    const uint16_t ca = (uint16_t)(asdu[4] | (asdu[5] << 8));
    const uint32_t ioa = (uint32_t)(asdu[6] | (asdu[7] << 8) | (asdu[8] << 16));

    if (type == C_IC_NA && cot == COT_ACT) {
        // General interrogation: act-con, all points (interrogated), act-term.
        const uint8_t qoi = asdu[9];
        uint8_t ic_ie[1] = { qoi };
        uint8_t a[20];
        size_t n = buildAsdu(a, C_IC_NA, COT_ACTCON, false, org, ca, 0, ic_ie, 1);
        sendI(w, cs, a, n);

        sub::Point pts[64];
        const size_t count = s.enumerate(pts, 64);
        for (size_t i = 0; i < count; i++) {
            sendPoint(w, cs, org, ca, pts[i], COT_INTROGEN);
        }
        n = buildAsdu(a, C_IC_NA, COT_ACTTERM, false, org, ca, 0, ic_ie, 1);
        sendI(w, cs, a, n);
        return w.len;
    }

    if (type == C_SC_NA) {
        const uint8_t sco = asdu[9];
        const bool on = (sco & 0x01) != 0;
        const sub::CmdResult r = s.command(ioa, cot, org, on, now_ms);
        const bool negative = (r == sub::CmdResult::Dropped || r == sub::CmdResult::Alarm ||
                               r == sub::CmdResult::Unknown);
        uint8_t ie[1] = { sco };
        uint8_t a[20];
        // Activation confirmation (positive or negative)
        size_t n = buildAsdu(a, C_SC_NA, COT_ACTCON, negative, org, ca, ioa, ie, 1);
        sendI(w, cs, a, n);
        if (!negative) {
            // If the real breaker moved, report it spontaneously, then terminate the activation.
            if (r == sub::CmdResult::Accepted) {
                sub::Point bp { sub::IOA_BREAKER, 1, s.breakerOpen() ? 1 : 0 };
                sendPoint(w, cs, org, ca, bp, COT_SPONT);
            }
            n = buildAsdu(a, C_SC_NA, COT_ACTTERM, false, org, ca, ioa, ie, 1);
            sendI(w, cs, a, n);
        }
        return w.len;
    }

    // Unhandled ASDU: just acknowledge.
    sendS(w, cs);
    return w.len;
}

} // namespace ot::net::iec104
