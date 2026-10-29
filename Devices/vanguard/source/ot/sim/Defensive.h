// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "HistSqlUnion.h"   // ot::sim::histsqlunion::FLAG - the authoritative #07 SQLi flag

#include <cstddef>
#include <cstdint>

// Shared Defensive evidence + investigation logic (host-tested). This is the single, deterministic
// evidence store behind the Defensive challenges, plus their completion predicates. The web surface
// (net/DefWeb, device-only) is a thin view onto this state; nothing here depends on ESP-IDF, so it
// compiles and runs on the host for testing.
//
// One consistent incident ties the evidence together:
//   * An engineering asset inventory lists the trusted substation/plant devices (name, IP, MAC,
//     vendor) and a self-contained OUI registry maps MAC prefixes to vendors.
//   * A saved PROFINET DCP capture shows an IMPOSTOR announcing a trusted device's name + IP from a
//     MAC whose OUI is not a trusted vendor (Device Takeover, #10).
//   * The historian access log shows an EXTERNAL address exporting a reactor tag, which draws the
//     investigator to the historian; its web console (net/HistWeb -> sim/HistSqlUnion) exposes a
//     SQL-injectable Filter-by-Source query from which the flag is extracted (Historian SQL Injection, #07).
//   * A second historian series has a one-sample gap a nightly backup still retains; restoring that
//     record and holding it stable clears Trojan Historian (#09).
//   * Seeded webmail points the investigator at each of the three evidence surfaces.
//
// PROVISIONAL VALUES: the source spec does not fix any MAC, OUI, IP, timestamp, tag value or flag for
// these challenges. Every concrete value below is PROVISIONAL, chosen for a self-contained,
// deterministic lab (documentation ranges: RFC 5737 203.0.113.0/24 for the external address; locally
// administered MACs with made-up, registry-backed OUIs). They are NOT claimed to be source-approved
// and carry no real-world vendor meaning.
namespace ot::def {

constexpr size_t def_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }

// ----- flags (flag{...}, <= 32 bytes, matching the repo convention) -------------------------------
// #07 is the Historian SQL Injection challenge: its authoritative flag IS the SQLi flag the player
// extracts from the simulated historian database (sim/HistSqlUnion), so this aliases that single
// constant rather than duplicating the literal (the registry/simulator/submission cannot drift).
constexpr const char* FORENSICS_FLAG = ot::sim::histsqlunion::FLAG;
constexpr const char* TROJAN_FLAG    = "flag{tr0jan_h1st_r3st0r3d}";
constexpr const char* DEVICE_FLAG    = "flag{dcp_1mp0st0r_unm4sk3d}";
static_assert(def_cstrlen(FORENSICS_FLAG) <= 32, "FORENSICS_FLAG must fit 32 bytes");
static_assert(def_cstrlen(TROJAN_FLAG) <= 32, "TROJAN_FLAG must fit 32 bytes");
static_assert(def_cstrlen(DEVICE_FLAG) <= 32, "DEVICE_FLAG must fit 32 bytes");

// ----- seeded evidence (read-only, deterministic) -------------------------------------------------

// Webmail is modelled as threaded conversations (an IR mailbox), not a flat inbox. A thread starts
// with the original report and may carry one or two realistic follow-ups; a later reply can introduce
// a detail that was not in the opening message. Each message may carry at most one attachment (an
// evidence file), so evidence is found by reading the conversation it belongs to rather than from a
// separate "all the answers" page.
struct MailMsg {
    const char* from;
    const char* date;       // provisional ISO-ish timestamp
    const char* body;       // plain text
    const char* attachment; // evidence file name, or nullptr
};

struct MailThread {
    const char* subject;
    const MailMsg* msgs;    // chronological
    int msgCount;
};

struct Asset {
    const char* name;      // engineering device tag, e.g. "PLC-01"
    const char* role;
    const char* ip;
    const char* mac;       // canonical upper-hex "AA:BB:CC:DD:EE:FF"
    const char* vendor;    // the vendor the inventory says owns this device
    const char* firmware;
};

struct OuiEntry {
    const char* oui;       // "AA:BB:CC"
    const char* vendor;
};

// One saved frame from the overnight capture (deterministic), with the fields needed to render a
// realistic Wireshark/tshark plain-text export. Most frames are PROFINET DCP; a few ARP/LLDP frames are
// background noise. The impostor is one PN-DCP frame whose NameOfStation collides with a trusted
// station announced earlier from a different, registry-untrusted MAC.
struct DcpFrame {
    const char* ts;         // capture timestamp
    const char* srcMac;     // Ethernet source (announcing MAC; canonical upper-case form)
    const char* dstMac;     // Ethernet destination (DCP multicast or unicast)
    const char* proto;      // "PN-DCP" / "ARP" / "LLDP"
    const char* deviceName; // DCP NameOfStation (announced station name); "" when not applicable
    const char* ip;         // DCP IPParameter / ARP sender IP; "" when none
    const char* info;       // Wireshark Info-column summary
};

struct AccessLog {
    const char* ts;
    const char* user;
    const char* srcip;
    const char* action;    // READ / EXPORT / WRITE
    const char* tag;
    bool external;         // true if srcip is outside the engineering subnet
};

// Evidence accessors (all deterministic, no state).
int threadCount();
const MailThread& thread(int i);
// Total messages across all threads (for the inbox summary / tests).
int mailMsgCount();

int assetCount();
const Asset& asset(int i);
const Asset* assetByName(const char* name);   // nullptr if unknown

int ouiCount();
const OuiEntry& oui(int i);
const char* vendorForMac(const char* mac);     // registry lookup by OUI; nullptr if unknown

int dcpCount();
const DcpFrame& dcp(int i);

int accessLogCount();
const AccessLog& accessLog(int i);

// ----- engineering reference documents (text/plain evidence for the protocol challenges) ----------
// Realistic archived OT documentation for the challenges whose protocol/point detail is otherwise
// only discoverable from the live wire: the IEC-104 breaker RTU (#01), the SIS maintenance interface
// (#02), the GOOSE training monitor (#06) and the SNMP field-device recovery (#08). Each describes
// the device / point / command semantics a player needs to REASON toward the solve (a point list, a
// protocol/command reference, a maintenance record, a recovery runbook) WITHOUT handing over a
// ready-made command. net/DefWeb renders them verbatim at /evidence?f=<name>; the host tests assert
// each doc stays numerically consistent with the matching simulator constants, so it can never drift.
const char* rtu560PointsDoc();   // #01 - IEC-104 RTU560 engineering point list + command reference
const char* sisMaintDoc();       // #02 - TriStation-lite SIS maintenance-interface tech note
const char* gooseMonitorDoc();   // #06 - GOOSE training-monitor configuration note
const char* rtu07RecoveryDoc();  // #08 - field RTU-07 SNMP recovery runbook
const char* reactorIoMapDoc();   // #11 - Unit-1 reactor SCADA I/O map + safe-band envelope + SCRAM note

// Normalises a MAC to canonical "AA:BB:CC:DD:EE:FF" (accepts lower/upper, ':' or '-' separators).
// Returns false if the input is not 6 hex octets. `out` must hold >= 18 bytes.
bool normalizeMac(const char* in, char out[18]);

// ============ Challenge 07 - Historian Forensics (investigation / submission) =====================
//
// Historian SQL Injection (#07): the historian's Filter-by-Source query (net/HistWeb -> sim/HistSqlUnion)
// concatenates user input and is injectable. The player extracts the flag from the simulated `flags`
// table via UNION-based SQL injection and submits it here. Submitting the correct flag latches solved;
// opening a page never solves, and ordinary (non-injection) queries never reveal the flag.
namespace forensics {
    class State {
    public:
        void reset() { solved_ = false; }
        // Returns true iff `flag` equals the authoritative SQLi flag (FORENSICS_FLAG). Idempotent:
        // once solved it stays solved, so a repeat submission never changes anything.
        bool submit(const char* flag);
        bool solved() const { return solved_; }
        const char* flag() const { return FORENSICS_FLAG; }
    private:
        bool solved_ = false;
    };
}

// ============ Challenge 09 - Trojan Historian (backup / restore) ===================================
//
// A trojanised historian deleted one sample from a live trend and masked it: the live trend also has
// genuine comms-outage gaps (which the nightly backup lacks too), so the first gap is a decoy. The
// backup is a MULTI-TAG export, so the investigator must filter it to the live trend's tag, diff the
// two, and find the ONE seq present in the backup but absent from the live trend (the deleted record),
// read its value, restore it, then keep it stable for HOLD_MS (a wrong overwrite re-corrupts it).
// PROVISIONAL values. The web layer renders LIVE and BACKUP as two SEPARATE exports (no pre-joined
// diff, no gap marked), so finding the record requires real analysis, not reading a labelled table.
namespace trojan {
    constexpr uint32_t HOLD_MS = 10000;
    constexpr const char* SERIES = "TURBINE.VIB";
    constexpr uint32_t MISSING_SEQ = 53;   // PROVISIONAL: the deleted record's seq (in backup, not live)
    constexpr int MISSING_VALUE = 742;     // PROVISIONAL: value retained only in the backup

    struct Sample { const char* tag; uint32_t seq; int value; };

    int liveCount();
    const Sample& live(int i);     // live trend export (single tag; deletion + decoy-outage seqs absent)
    int backupCount();
    const Sample& backup(int i);   // nightly backup export (multi-tag; holds the deleted record)

    class State {
    public:
        void reset(uint32_t now_ms);
        void tick(uint32_t now_ms);
        // Restore a record. Correct (seq,value) starts/holds the stable timer; anything else corrupts
        // it and clears the timer. Returns true iff the restore matched the backup.
        bool restore(uint32_t seq, int value, uint32_t now_ms);
        bool restored() const { return restored_; }
        bool solved() const { return solved_; }
        // Remaining stable-hold time in ms (0 if not currently holding, or already solved). Lets the
        // web/LCD show honest "hold in progress" feedback without changing the hold mechanics.
        uint32_t holdRemainingMs(uint32_t now_ms) const;
        const char* flag() const { return TROJAN_FLAG; }
    private:
        bool restored_ = false;
        bool holding_ = false;
        uint32_t hold_since_ms_ = 0;
        bool solved_ = false;
    };
}

// ============ Challenge 10 - Device Takeover (PROFINET DCP identity audit) =========================
//
// From the saved DCP capture, one frame announces a trusted station's name + IP from a MAC whose OUI
// is not a registered vendor and does not match the inventory's MAC for that station: the impostor.
// The finding is the impostor's MAC. Submitting it latches solved and releases the flag. Passive
// identity investigation only - nothing changes a device.
namespace takeover {
    // Canonical answers derived from the seeded capture + inventory (not free constants).
    const char* impostorMac();
    const char* impostorTs();
    const char* spoofedName();

    class State {
    public:
        void reset() { solved_ = false; }
        // Correct iff the submitted MAC normalises to the impostor's MAC.
        bool submit(const char* mac);
        bool solved() const { return solved_; }
        const char* flag() const { return DEVICE_FLAG; }
    private:
        bool solved_ = false;
    };
}

} // namespace ot::def
