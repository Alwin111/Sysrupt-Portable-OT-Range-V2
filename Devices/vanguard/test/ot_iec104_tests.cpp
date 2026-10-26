// SPDX-License-Identifier: Apache-2.0
// Host-side tests for the IEC-104 protocol layer over the substation (Challenge 4).
// Run: g++ -std=c++17 -I../source/ot ot_iec104_tests.cpp ../source/ot/net/Iec104.cpp ../source/ot/sim/Substation.cpp && ./a.out
#include "net/Iec104.h"
#include "sim/Substation.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace ot;
using namespace ot::net::iec104;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

// Builds a client I-frame APDU carrying a one-object ASDU. ie points at the information element.
static size_t clientI(uint8_t* apdu, uint8_t type, uint8_t cot, uint8_t org, uint16_t ca,
                      uint32_t ioa, const uint8_t* ie, size_t ieLen) {
    size_t n = 0;
    apdu[n++] = 0x68;
    const size_t lenPos = n++;
    apdu[n++] = 0x00; apdu[n++] = 0x00; apdu[n++] = 0x00; apdu[n++] = 0x00; // NS=0, NR=0
    apdu[n++] = type; apdu[n++] = 0x01; apdu[n++] = (uint8_t)(cot & 0x3F);
    apdu[n++] = org; apdu[n++] = (uint8_t)(ca & 0xFF); apdu[n++] = (uint8_t)(ca >> 8);
    apdu[n++] = (uint8_t)(ioa & 0xFF); apdu[n++] = (uint8_t)((ioa >> 8) & 0xFF); apdu[n++] = (uint8_t)((ioa >> 16) & 0xFF);
    std::memcpy(apdu + n, ie, ieLen); n += ieLen;
    apdu[lenPos] = (uint8_t)(n - 2);
    return n;
}

// Scans the response buffer for the value of a given IOA (first I-frame match). Returns true if found.
static bool findIoa(const uint8_t* out, size_t len, uint32_t ioa, int32_t& value, uint8_t& type) {
    size_t i = 0;
    while (i + 2 <= len && out[i] == 0x68) {
        const uint8_t l = out[i + 1];
        const size_t apduLen = (size_t)l + 2;
        if (i + apduLen > len) break;
        const uint8_t c1 = out[i + 2];
        if ((c1 & 0x01) == 0) { // I-frame
            const uint8_t* asdu = out + i + 6;
            const uint8_t t = asdu[0];
            const uint32_t a = (uint32_t)(asdu[6] | (asdu[7] << 8) | (asdu[8] << 16));
            if (a == ioa) {
                type = t;
                if (t == 1) value = asdu[9] & 1;
                else value = (int16_t)(asdu[9] | (asdu[10] << 8));
                return true;
            }
        }
        i += apduLen;
    }
    return false;
}

// True if any I-frame in the buffer is an activation confirmation with the P/N (negative) bit set.
static bool hasNegativeConfirm(const uint8_t* out, size_t len) {
    size_t i = 0;
    while (i + 2 <= len && out[i] == 0x68) {
        const uint8_t l = out[i + 1];
        const size_t apduLen = (size_t)l + 2;
        if (i + apduLen > len) break;
        if ((out[i + 2] & 0x01) == 0) {
            const uint8_t* asdu = out + i + 6;
            if ((asdu[2] & 0x3F) == 7 && (asdu[2] & 0x40)) return true;
        }
        i += apduLen;
    }
    return false;
}

static const uint32_t UNLOCKED = sub::INTERROG_WINDOW_MS + 500;

static void testStartDt() {
    sub::Substation s; s.reset(0);
    ConnState cs;
    const uint8_t startdt[6] = { 0x68, 0x04, 0x07, 0x00, 0x00, 0x00 };
    uint8_t out[1024];
    const size_t n = processApdu(s, 0, startdt, 6, cs, out, sizeof(out));
    CHECK(n == 6 && out[2] == 0x0B); // STARTDT_CON
}

static void testInterrogationExposesOriginator() {
    sub::Substation s; s.reset(0);
    ConnState cs;
    uint8_t apdu[32];
    uint8_t qoi[1] = { 20 };
    const size_t m = clientI(apdu, 100, 6, 0, sub::COMMON_ADDR, 0, qoi, 1);
    uint8_t out[2048];
    const size_t n = processApdu(s, UNLOCKED, apdu, m, cs, out, sizeof(out));
    int32_t v; uint8_t t;
    CHECK(findIoa(out, n, sub::IOA_LAST_LEGIT_ORIG, v, t) && v == sub::SUBSTATION_ORIG);
    CHECK(findIoa(out, n, sub::IOA_BREAKER, v, t) && v == 0); // breaker closed
}

static void testWrongOriginatorRejected() {
    sub::Substation s; s.reset(0);
    ConnState cs;
    uint8_t apdu[32];
    uint8_t sco[1] = { 0x01 }; // SCS = on
    const size_t m = clientI(apdu, 45, 6, 99, sub::COMMON_ADDR, sub::IOA_BREAKER, sco, 1);
    uint8_t out[512];
    const size_t n = processApdu(s, UNLOCKED, apdu, m, cs, out, sizeof(out));
    CHECK(hasNegativeConfirm(out, n));
    CHECK(!s.breakerOpen());
}

static void testCorrectTripOpensAndFlag() {
    sub::Substation s; s.reset(0);
    ConnState cs;
    uint8_t apdu[32];
    uint8_t sco[1] = { 0x00 }; // SCS=0 (OFF) = breaker TRIP/open, per the guide + IEC convention
    const size_t m = clientI(apdu, 45, 6, sub::SUBSTATION_ORIG, sub::COMMON_ADDR, sub::IOA_BREAKER, sco, 1);
    uint8_t out[512];
    const size_t n = processApdu(s, UNLOCKED, apdu, m, cs, out, sizeof(out));
    CHECK(!hasNegativeConfirm(out, n));
    CHECK(s.breakerOpen());
    CHECK(s.solved());

    // Re-interrogate and recover the flag from the flag object block.
    uint8_t qoi[1] = { 20 };
    const size_t m2 = clientI(apdu, 100, 6, 0, sub::COMMON_ADDR, 0, qoi, 1);
    uint8_t out2[2048];
    const size_t n2 = processApdu(s, UNLOCKED, apdu, m2, cs, out2, sizeof(out2));
    std::string flag;
    for (uint16_t i = 0; i < sub::FLAG_LEN_OBJS; i++) {
        int32_t v; uint8_t t;
        if (findIoa(out2, n2, sub::IOA_FLAG_BASE + i, v, t)) {
            uint16_t w = (uint16_t)v;
            char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
            if (hi) flag += hi;
            if (lo) flag += lo;
        }
    }
    CHECK(flag == std::string(sub::IEC104_FLAG));
}

// SCS=1 (ON = close) from the TRUSTED originator is ACCEPTED (positive ACTCON) but does NOT trip the
// Feeder-1 breaker - the exact trap a first-time player hits (a wrong-polarity command that still
// confirms). The RTU then spontaneously reports IOA 1001 = 0 (still closed); that position report is
// the feedback the player-facing clues now point to ("confirm the position, not the ACTCON").
static void testOnFromTrustedAcceptedButNoTrip() {
    sub::Substation s; s.reset(0);
    ConnState cs;
    uint8_t apdu[32];
    uint8_t sco[1] = { 0x01 }; // SCS=1 (ON) = close, from the trusted originator
    const size_t m = clientI(apdu, 45, 6, sub::SUBSTATION_ORIG, sub::COMMON_ADDR, sub::IOA_BREAKER, sco, 1);
    uint8_t out[512];
    const size_t n = processApdu(s, UNLOCKED, apdu, m, cs, out, sizeof(out));
    CHECK(!hasNegativeConfirm(out, n)); // accepted: positive ACTCON (looks like success)
    CHECK(!s.breakerOpen());            // but the breaker did not open
    CHECK(!s.solved());                 // and the challenge is NOT solved
    // Re-interrogate - the "confirm the position, not the ACTCON" verification the clues now describe.
    // The position report shows IOA 1001 still = 0 (closed), trip-confirmed still 0, flag block still 0.
    uint8_t qoi[1] = { 20 };
    const size_t m2 = clientI(apdu, 100, 6, 0, sub::COMMON_ADDR, 0, qoi, 1);
    uint8_t out2[2048];
    const size_t n2 = processApdu(s, UNLOCKED, apdu, m2, cs, out2, sizeof(out2));
    int32_t v; uint8_t t;
    CHECK(findIoa(out2, n2, sub::IOA_BREAKER, v, t) && v == 0);        // breaker still closed
    CHECK(findIoa(out2, n2, sub::IOA_TRIP_CONFIRMED, v, t) && v == 0); // trip-confirmed still false
    CHECK(findIoa(out2, n2, sub::IOA_FLAG_BASE, v, t) && v == 0);      // flag block still empty
}

int main() {
    testStartDt();
    testInterrogationExposesOriginator();
    testWrongOriginatorRejected();
    testOnFromTrustedAcceptedButNoTrip();
    testCorrectTripOpensAndFlag();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
