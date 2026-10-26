// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 06 - Second Blackout. Pure logic (host-tested); net/Goose is the wire codec and
// GooseService wraps it with a UDP listener.
//
// Scenario: substation relays subscribe to IEC 61850 GOOSE multicast, which has no authentication. A
// subscriber accepts any frame on its APPID whose state number (stNum) is newer than the last seen -
// that is treated as a genuine state change. The attacker observes the normal stream (breaker closed,
// stNum stable, sqNum incrementing) and forges the next state change with the breaker-open dataset
// and stNum = last+1, which the relay accepts: a second blackout.
//
// Solve = a frame is accepted (matching APPID + confRev, not a test frame, stNum strictly greater
// than the last accepted) that carries breakerOpen=true. Stale/duplicate stNum, wrong APPID, wrong
// confRev and test frames are rejected and never trip the breaker.
//
// SIMULATION LIMITATION: real GOOSE is ASN.1/BER over Ethertype 0x88B8 on an L2 multicast group. That
// is not reachable from the badge's LWIP/Wi-Fi stack, so net/Goose uses a fixed-layout UDP frame.
// This is a TRAINING SIMULATION of GOOSE semantics (APPID + stNum/sqNum + state change acceptance),
// NOT a conformant GOOSE implementation.
//
// PROVISIONAL: the source spec does not fix the APPID, confRev or flag; the values below are chosen
// for deterministic testing.
namespace ot::goose {

constexpr uint16_t UDP_PORT = 6180;      // PROVISIONAL (training UDP encapsulation; not L2 GOOSE)
constexpr uint16_t EXPECTED_APPID = 0x0BB8; // PROVISIONAL subscribed APPID
constexpr uint16_t EXPECTED_CONFREV = 1;    // PROVISIONAL configuration revision

constexpr const char* GOOSE_FLAG = "flag{g00se_r3play_bl4ck0ut}";
constexpr size_t goose_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(goose_cstrlen(GOOSE_FLAG) <= 32, "GOOSE_FLAG must fit 32 bytes");

// Decoded logical GOOSE PDU (what the subscriber reasons about).
struct Pdu {
    uint16_t appid;
    uint32_t stNum;
    uint32_t sqNum;
    uint16_t confRev;
    bool breakerOpen;   // the single boolean in the modelled dataset
    bool test;          // test flag: process rejects test frames
};

enum class Result {
    Accepted,       // newer state change accepted (breaker state updated)
    RejectedAppid,  // not our subscription
    RejectedConfRev,// configuration revision mismatch
    RejectedTest,   // test frame - ignored by process
    RejectedStale,  // stNum not newer than last accepted (duplicate / replay / stale)
};

class GooseSim {
public:
    void reset();

    // Feed the normal publisher stream (breaker closed, incrementing stNum) so the subscriber has a
    // realistic "last accepted" baseline before the attack. Returns the resulting Result.
    Result process(const Pdu& p);

    uint32_t lastStNum() const { return last_st_; }
    bool breakerOpen() const { return breaker_open_; }
    bool solved() const { return solved_; }
    const char* flag() const { return GOOSE_FLAG; }

private:
    bool seeded_ = false;
    uint32_t last_st_ = 0;
    bool breaker_open_ = false;
    bool solved_ = false;
};

} // namespace ot::goose
