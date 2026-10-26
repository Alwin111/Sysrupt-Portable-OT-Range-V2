// SPDX-License-Identifier: Apache-2.0
// Host tests for Challenge 08 - Field-Device Recovery: the SnmpDevice recovery logic + the SNMP
// BER wire codec (GET / GETNEXT / SET round-trips and a full recovery via the codec).
// Run: g++ -std=c++17 -I../source/ot ot_snmp_tests.cpp ../source/ot/sim/SnmpDevice.cpp ../source/ot/net/Snmp.cpp && ./a.out
#include "sim/SnmpDevice.h"
#include "net/Snmp.h"

#include <cstdio>
#include <cstring>

using namespace ot::snmp;
namespace wire = ot::net::snmpwire;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

// OIDs under test.
static const uint32_t OID_NAME[]    = {1,3,6,1,2,1,1,5,0};
static const uint32_t OID_MODE[]    = {1,3,6,1,4,1,42424,1,0};
static const uint32_t OID_RUN[]     = {1,3,6,1,4,1,42424,2,0};
static const uint32_t OID_GOOD[]    = {1,3,6,1,4,1,42424,3,0};
static const uint32_t OID_RESTORE[] = {1,3,6,1,4,1,42424,4,0};
static const uint32_t OID_HEALTH[]  = {1,3,6,1,4,1,42424,5,0};

// ---- minimal SNMP request builders (mirror the codec's wire format) ----
static size_t encLen(uint8_t* o, size_t n) {
    if (n < 0x80) { o[0] = (uint8_t)n; return 1; }
    o[0] = 0x81; o[1] = (uint8_t)n; return 2;
}
static size_t wrap(uint8_t tag, const uint8_t* c, size_t cl, uint8_t* o) {
    uint8_t lb[2]; size_t ln = encLen(lb, cl);
    o[0] = tag; memcpy(o + 1, lb, ln); memcpy(o + 1 + ln, c, cl); return 1 + ln + cl;
}
static size_t encInt(int32_t v, uint8_t* o) {
    uint8_t t[5]; int n = 0; uint32_t u = (uint32_t)v;
    uint8_t b[4] = {(uint8_t)(u>>24),(uint8_t)(u>>16),(uint8_t)(u>>8),(uint8_t)u};
    int s = 0; while (s<3 && ((b[s]==0&&(b[s+1]&0x80)==0)||(b[s]==0xff&&(b[s+1]&0x80)))) s++;
    for (int i=s;i<4;i++) t[n++]=b[i];
    return wrap(0x02, t, n, o);
}
static size_t encOid(const uint32_t* oid, size_t n, uint8_t* o) {
    uint8_t body[48]; size_t p=0; body[p++]=(uint8_t)(oid[0]*40+oid[1]);
    for (size_t i=2;i<n;i++){uint32_t v=oid[i];uint8_t tmp[5];int tn=0;tmp[tn++]=v&0x7f;v>>=7;while(v){tmp[tn++]=(v&0x7f)|0x80;v>>=7;}for(int k=tn-1;k>=0;k--)body[p++]=tmp[k];}
    return wrap(0x06, body, p, o);
}
static size_t encStr(const char* s, uint8_t* o) { return wrap(0x04, (const uint8_t*)s, strlen(s), o); }

// Build a GET/GETNEXT/SET datagram for one OID (value: INTEGER for SET, NULL otherwise).
static size_t buildReq(uint8_t pdu, const char* community, const uint32_t* oid, size_t oidn,
                       bool isSet, int32_t setVal, uint8_t* out) {
    uint8_t vb[64]; size_t vp = 0;
    vp += encOid(oid, oidn, vb + vp);
    if (isSet) vp += encInt(setVal, vb + vp);
    else { vb[vp++] = 0x05; vb[vp++] = 0x00; } // NULL
    uint8_t vbseq[80]; size_t s = wrap(0x30, vb, vp, vbseq);
    uint8_t vbl[96]; size_t vl = wrap(0x30, vbseq, s, vbl);
    uint8_t pduc[160]; size_t pp = 0;
    pp += encInt(1, pduc + pp);  // request-id
    pp += encInt(0, pduc + pp);  // error-status
    pp += encInt(0, pduc + pp);  // error-index
    memcpy(pduc + pp, vbl, vl); pp += vl;
    uint8_t pdub[200]; size_t pb = wrap(pdu, pduc, pp, pdub);
    uint8_t body[256]; size_t bp = 0;
    bp += encInt(1, body + bp);  // version v2c
    bp += encStr(community, body + bp);
    memcpy(body + bp, pdub, pb); bp += pb;
    return wrap(0x30, body, bp, out);
}
static bool respHas(const uint8_t* r, size_t n, const char* s) {
    size_t sl = strlen(s);
    for (size_t i = 0; i + sl <= n; i++) if (memcmp(r + i, s, sl) == 0) return true;
    return false;
}

int main() {
    // ---- device recovery logic ----
    {
        SnmpDevice d; d.reset(0);
        CHECK(d.get(OID_RUN, 9).ival == ATTACKER_IMAGE);
        CHECK(d.get(OID_GOOD, 9).ival == LAST_GOOD_IMAGE);
        CHECK(d.get(OID_HEALTH, 9).ival == 0);
        CHECK(!d.healthy() && !d.solved());
        // SET gating: no/ wrong community rejected
        CHECK(d.setInteger(OID_MODE, 9, 1, "public", 0) == SnmpErr::AuthorizationError);
        CHECK(d.setInteger(OID_MODE, 9, 1, WRITE_COMMUNITY, 0) == SnmpErr::None);
        // restore outside recovery mode fails
        SnmpDevice d2; d2.reset(0);
        CHECK(d2.setInteger(OID_RESTORE, 9, LAST_GOOD_IMAGE, WRITE_COMMUNITY, 0) == SnmpErr::NoAccess);
        // read-only OID not writable
        CHECK(d.setInteger(OID_RUN, 9, 5, WRITE_COMMUNITY, 0) == SnmpErr::NotWritable);
    }
    {
        // wrong restore value leaves it unhealthy; correct value + exit recovery -> healthy -> solved
        SnmpDevice d; d.reset(0);
        CHECK(d.setInteger(OID_MODE, 9, 1, WRITE_COMMUNITY, 0) == SnmpErr::None);
        CHECK(d.setInteger(OID_RESTORE, 9, 1234, WRITE_COMMUNITY, 0) == SnmpErr::None); // wrong image
        CHECK(!d.healthy());
        CHECK(d.setInteger(OID_RESTORE, 9, LAST_GOOD_IMAGE, WRITE_COMMUNITY, 0) == SnmpErr::None);
        CHECK(!d.healthy()); // still in recovery mode
        CHECK(d.setInteger(OID_MODE, 9, 0, WRITE_COMMUNITY, 0) == SnmpErr::None);
        CHECK(d.healthy());  // running == last-good AND back in run mode
        d.tick(0); CHECK(!d.solved());               // hold timer just started
        d.tick(RECOVERY_HOLD_MS + 1);
        CHECK(d.solved());                            // verified recovered
        CHECK(std::strcmp(d.get((const uint32_t[]){1,3,6,1,4,1,42424,9,0}, 9).sval, SNMP_FLAG) == 0);
    }
    // ---- GETNEXT walk ordering ----
    {
        SnmpDevice d; d.reset(0);
        uint32_t no[24]; size_t nl = 0;
        const uint32_t start[] = {1,3,6,1}; // walk from the subtree root
        Value v = d.getNext(start, 4, no, &nl, 24);
        CHECK(v.type != ValType::NoSuchObject && nl == 9 && no[7] == 1); // first = sysDescr .1.1.1.0
    }

    // ---- codec round-trips ----
    {
        SnmpDevice d; d.reset(0);
        uint8_t req[256], resp[700];
        // GET sysName with the read community -> response contains "RTU-07"
        size_t rn = buildReq(wire::PDU_GET, "public", OID_NAME, 9, false, 0, req);
        size_t on = wire::process(d, req, rn, 0, resp, sizeof(resp));
        CHECK(on > 0 && respHas(resp, on, "RTU-07"));
        // GET with wrong read community -> dropped (no response)
        rn = buildReq(wire::PDU_GET, "wrong", OID_NAME, 9, false, 0, req);
        CHECK(wire::process(d, req, rn, 0, resp, sizeof(resp)) == 0);
        // Full recovery through the codec
        rn = buildReq(wire::PDU_SET, WRITE_COMMUNITY, OID_MODE, 9, true, 1, req);
        CHECK(wire::process(d, req, rn, 0, resp, sizeof(resp)) > 0);
        rn = buildReq(wire::PDU_SET, WRITE_COMMUNITY, OID_RESTORE, 9, true, LAST_GOOD_IMAGE, req);
        CHECK(wire::process(d, req, rn, 0, resp, sizeof(resp)) > 0);
        rn = buildReq(wire::PDU_SET, WRITE_COMMUNITY, OID_MODE, 9, true, 0, req);
        CHECK(wire::process(d, req, rn, 0, resp, sizeof(resp)) > 0);
        CHECK(d.healthy());
        d.tick(0);                     // start the recovery-verification hold timer
        d.tick(RECOVERY_HOLD_MS + 1);  // held healthy long enough -> verified recovered
        CHECK(d.solved());
        // SET via the codec with the wrong community -> authorizationError (non-zero error-status),
        // and the device must NOT change (fresh device stays unhealthy).
        SnmpDevice d3; d3.reset(0);
        rn = buildReq(wire::PDU_SET, "public", OID_MODE, 9, true, 1, req);
        size_t o3 = wire::process(d3, req, rn, 0, resp, sizeof(resp));
        CHECK(o3 > 0 && d3.get(OID_MODE, 9).ival == 0); // recovery mode unchanged
    }

    printf("snmp tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
