// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 08 - Field-Device Recovery (Defensive). Pure logic (host-tested); net/Snmp is the wire
// codec and SnmpService wraps it with a UDP listener on port 161.
//
// Scenario: a field RTU (RTU-07) is running a TAMPERED firmware image after an incident. Its SNMP
// agent is reachable with the read community "public". The responder/defender must investigate it
// over SNMP, discover it is unhealthy (running an image that is NOT the device's last-good image),
// and RECOVER it to the known-good firmware through the device's own recovery workflow, then verify
// the device reports healthy again.
//
// Recovery workflow (intended player path):
//   1. GET/walk with community "public": sysDescr is garbled "[IMAGE CORRUPT]"; fwRunningImage !=
//      fwLastGoodImage; deviceHealth = 0 (unhealthy). sysContact discloses the write community.
//   2. SET fwRecoveryMode = 1 (needs the write community) -> device enters recovery mode.
//   3. SET fwRestoreImage = <fwLastGoodImage value> (needs the write community AND recovery mode) ->
//      the running image is restored to that value. Writing the wrong value leaves it unhealthy.
//   4. SET fwRecoveryMode = 0 -> return to normal run.
//   5. deviceHealth becomes 1 (running image == last-good image AND back in run mode). Held healthy
//      for RECOVERY_HOLD_MS, the device is verified RECOVERED and exposes the flag at the flag OID.
//
// Recovery predicate (genuine recovery, NOT compromise): solved iff the RUNNING image equals the
// device's own LAST-GOOD image and the device is back in normal run mode (recovery mode cleared),
// held continuously for RECOVERY_HOLD_MS. Writing an arbitrary/attacker image never solves it.

namespace ot::snmp {

constexpr uint16_t UDP_PORT = 161;

constexpr const char* READ_COMMUNITY = "public";
constexpr const char* WRITE_COMMUNITY = "r3c0v3ry-07"; // disclosed in sysContact (OID 1.3.6.1.2.1.1.4.0)

constexpr int32_t ATTACKER_IMAGE = 9001;   // tampered image running after the incident
constexpr int32_t LAST_GOOD_IMAGE = 341;    // the device's known-good image (firmware 3.4.1)

constexpr uint32_t RECOVERY_HOLD_MS = 3000; // device must read healthy this long to be "verified"

constexpr const char* SNMP_FLAG = "flag{snmp_f1eld_dev_r3st0r3d}";
constexpr size_t snmp_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(snmp_cstrlen(SNMP_FLAG) <= 64, "SNMP_FLAG must fit the flag OCTET STRING");

// SNMP value kinds this device uses.
enum class ValType { Integer, OctetString, NoSuchObject };

struct Value {
    ValType type = ValType::NoSuchObject;
    int32_t ival = 0;
    char sval[80] = {};
};

// SNMP error-status codes (subset) returned to the codec.
enum class SnmpErr : int { None = 0, TooBig = 1, NoSuchName = 2, BadValue = 3, ReadOnly = 4,
                           GenErr = 5, NoAccess = 6, WrongType = 7, NotWritable = 17,
                           AuthorizationError = 16 };

class SnmpDevice {
public:
    void reset(uint32_t now_ms);
    void tick(uint32_t now_ms);

    // GET one OID (sub-identifiers + count). Returns the value (NoSuchObject if unknown).
    Value get(const uint32_t* oid, size_t len) const;

    // GETNEXT: fills outOid/outLen with the lexicographically next managed OID after (oid,len) and
    // returns its value. Returns NoSuchObject (end-of-MIB) when there is none.
    Value getNext(const uint32_t* oid, size_t len, uint32_t* outOid, size_t* outLen, size_t outCap) const;

    // SET one OID to an integer. `community` is the SNMP community from the request. Returns the
    // SNMP error-status (None on success). Enforces community + recovery-mode gating.
    SnmpErr setInteger(const uint32_t* oid, size_t len, int32_t value, const char* community, uint32_t now_ms);

    bool healthy() const { return running_image_ == LAST_GOOD_IMAGE && recovery_mode_ == 0; }
    bool solved() const { return solved_; }
    int32_t runningImage() const { return running_image_; }

    // Count of managed OIDs (for tests / walk bounds).
    static size_t oidCount();

private:
    int32_t running_image_ = ATTACKER_IMAGE;
    int32_t recovery_mode_ = 0;        // 0 = normal run, 1 = recovery mode
    bool solved_ = false;
    bool hold_running_ = false;
    uint32_t hold_since_ms_ = 0;
};

} // namespace ot::snmp
