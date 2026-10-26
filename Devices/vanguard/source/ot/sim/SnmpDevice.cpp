// SPDX-License-Identifier: Apache-2.0
#include "SnmpDevice.h"

#include <cstdio>
#include <cstring>

namespace ot::snmp {

namespace {

// Managed OIDs, in lexicographic order (required for GETNEXT / walk).
struct MibEntry { const uint32_t oid[12]; uint8_t len; };
constexpr MibEntry MIB[] = {
    {{1,3,6,1,2,1,1,1,0}, 9},            // sysDescr
    {{1,3,6,1,2,1,1,4,0}, 9},            // sysContact (write-community clue)
    {{1,3,6,1,2,1,1,5,0}, 9},            // sysName
    {{1,3,6,1,4,1,42424,1,0}, 9},        // fwRecoveryMode   (rw)
    {{1,3,6,1,4,1,42424,2,0}, 9},        // fwRunningImage   (ro)
    {{1,3,6,1,4,1,42424,3,0}, 9},        // fwLastGoodImage  (ro)
    {{1,3,6,1,4,1,42424,4,0}, 9},        // fwRestoreImage   (rw, reads 0)
    {{1,3,6,1,4,1,42424,5,0}, 9},        // deviceHealth     (ro)
    {{1,3,6,1,4,1,42424,9,0}, 9},        // recoveryFlag     (ro)
};
constexpr size_t MIB_N = sizeof(MIB) / sizeof(MIB[0]);

int cmpOid(const uint32_t* a, size_t al, const uint32_t* b, size_t bl) {
    const size_t n = al < bl ? al : bl;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    if (al == bl) return 0;
    return al < bl ? -1 : 1;
}
bool eqOid(const uint32_t* a, size_t al, const MibEntry& e) {
    return cmpOid(a, al, e.oid, e.len) == 0;
}

} // namespace

void SnmpDevice::reset(uint32_t) {
    running_image_ = ATTACKER_IMAGE;
    recovery_mode_ = 0;
    solved_ = false;
    hold_running_ = false;
    hold_since_ms_ = 0;
}

void SnmpDevice::tick(uint32_t now_ms) {
    // Verify genuine recovery: the device must read healthy (running the last-good image, back in
    // normal run mode) continuously for RECOVERY_HOLD_MS before it is declared recovered.
    if (healthy()) {
        if (!hold_running_) { hold_running_ = true; hold_since_ms_ = now_ms; }
        else if ((now_ms - hold_since_ms_) >= RECOVERY_HOLD_MS) { solved_ = true; }
    } else {
        hold_running_ = false;
    }
}

size_t SnmpDevice::oidCount() { return MIB_N; }

Value SnmpDevice::get(const uint32_t* oid, size_t len) const {
    Value v;
    // sysDescr
    static const uint32_t o_descr[] = {1,3,6,1,2,1,1,1,0};
    static const uint32_t o_contact[] = {1,3,6,1,2,1,1,4,0};
    static const uint32_t o_name[] = {1,3,6,1,2,1,1,5,0};
    static const uint32_t o_mode[] = {1,3,6,1,4,1,42424,1,0};
    static const uint32_t o_run[] = {1,3,6,1,4,1,42424,2,0};
    static const uint32_t o_good[] = {1,3,6,1,4,1,42424,3,0};
    static const uint32_t o_restore[] = {1,3,6,1,4,1,42424,4,0};
    static const uint32_t o_health[] = {1,3,6,1,4,1,42424,5,0};
    static const uint32_t o_flag[] = {1,3,6,1,4,1,42424,9,0};

    auto is = [&](const uint32_t* o) { return cmpOid(oid, len, o, 9) == 0; };

    if (is(o_descr)) {
        v.type = ValType::OctetString;
        if (healthy()) std::snprintf(v.sval, sizeof(v.sval), "Sysrupt RTU-07 firmware img=%d (OK)", (int)running_image_);
        else std::snprintf(v.sval, sizeof(v.sval), "Sysrupt RTU-07 firmware img=%d [IMAGE CORRUPT - recovery required]", (int)running_image_);
    } else if (is(o_contact)) {
        v.type = ValType::OctetString;
        std::snprintf(v.sval, sizeof(v.sval), "OT maintenance <noc@sysrupt.local>; SNMP write community: %s", WRITE_COMMUNITY);
    } else if (is(o_name)) {
        v.type = ValType::OctetString;
        std::snprintf(v.sval, sizeof(v.sval), "RTU-07");
    } else if (is(o_mode)) {
        v.type = ValType::Integer; v.ival = recovery_mode_;
    } else if (is(o_run)) {
        v.type = ValType::Integer; v.ival = running_image_;
    } else if (is(o_good)) {
        v.type = ValType::Integer; v.ival = LAST_GOOD_IMAGE;
    } else if (is(o_restore)) {
        v.type = ValType::Integer; v.ival = 0; // write-only register: reads back 0
    } else if (is(o_health)) {
        v.type = ValType::Integer; v.ival = healthy() ? 1 : 0;
    } else if (is(o_flag)) {
        v.type = ValType::OctetString;
        std::snprintf(v.sval, sizeof(v.sval), "%s", solved_ ? SNMP_FLAG : "");
    }
    return v; // NoSuchObject if nothing matched
}

Value SnmpDevice::getNext(const uint32_t* oid, size_t len, uint32_t* outOid, size_t* outLen, size_t outCap) const {
    const MibEntry* best = nullptr;
    for (size_t i = 0; i < MIB_N; i++) {
        if (cmpOid(MIB[i].oid, MIB[i].len, oid, len) > 0) { best = &MIB[i]; break; } // MIB is sorted
    }
    if (best == nullptr) { return Value{}; } // end of MIB
    if (outCap < best->len) { return Value{}; }
    for (size_t i = 0; i < best->len; i++) outOid[i] = best->oid[i];
    *outLen = best->len;
    return get(best->oid, best->len);
}

SnmpErr SnmpDevice::setInteger(const uint32_t* oid, size_t len, int32_t value, const char* community, uint32_t now_ms) {
    (void)now_ms;
    static const uint32_t o_mode[] = {1,3,6,1,4,1,42424,1,0};
    static const uint32_t o_restore[] = {1,3,6,1,4,1,42424,4,0};
    const bool writeAuthed = community != nullptr && std::strcmp(community, WRITE_COMMUNITY) == 0;

    if (cmpOid(oid, len, o_mode, 9) == 0) {
        if (!writeAuthed) return SnmpErr::AuthorizationError;
        if (value != 0 && value != 1) return SnmpErr::BadValue;
        recovery_mode_ = value;
        return SnmpErr::None;
    }
    if (cmpOid(oid, len, o_restore, 9) == 0) {
        if (!writeAuthed) return SnmpErr::AuthorizationError;
        // The firmware image can only be rewritten while the device is in recovery mode.
        if (recovery_mode_ != 1) return SnmpErr::NoAccess;
        running_image_ = value; // writing the wrong value leaves the device unhealthy (not recovered)
        return SnmpErr::None;
    }
    // Any other managed OID is read-only; unknown OIDs do not exist.
    for (size_t i = 0; i < MIB_N; i++) {
        if (eqOid(oid, len, MIB[i])) return SnmpErr::NotWritable;
    }
    return SnmpErr::NoSuchName;
}

} // namespace ot::snmp
