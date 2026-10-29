// SPDX-License-Identifier: Apache-2.0
// Host tests for the shared Defensive evidence + investigation logic (#07 Historian Forensics,
// #09 Trojan Historian, #10 Device Takeover).
// Run: g++ -std=c++17 -I../source/ot ot_defensive_tests.cpp ../source/ot/sim/Defensive.cpp && ./a.out
#include "sim/Defensive.h"
#include "sim/Substation.h"   // #01 constants the rtu560_points doc must match
#include "sim/SafetySystem.h" // #02 constants the sis_maint_note doc must match
#include "sim/GooseSim.h"     // #06 constants the goose_monitor doc must match
#include "sim/SnmpDevice.h"   // #08 constants the rtu07_recovery doc must match
#include "sim/Registers.h"    // #11 register map the reactor_io_map doc must match
#include "sim/PlantConfig.h"  // #11 safe-band / SCRAM constants the reactor_io_map doc must match

#include <cstdio>
#include <cstring>

using namespace ot::def;

namespace { int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; } }
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

int main() {
    // ----- evidence present + internally consistent -----
    CHECK(threadCount() == 11); // 10 original conversations + the new pump-bus (#04) thread
    CHECK(mailMsgCount() >= threadCount()); // threads may carry follow-up replies
    CHECK(assetCount() == 5);   // PLC-01, RTU-02, HMI-03, HIST-04 + RTU-07 (unresponsive, #08 discovery)
    CHECK(dcpCount() == 12);
    CHECK(accessLogCount() == 5);
    CHECK(ouiCount() == 4);

    // MAC normalisation accepts mixed case / separators and rejects malformed input.
    {
        char m[18];
        CHECK(normalizeMac("0a-77-88-00-00-aa", m) && std::strcmp(m, "0A:77:88:00:00:AA") == 0);
        CHECK(normalizeMac("0A:11:22:00:00:21", m) && std::strcmp(m, "0A:11:22:00:00:21") == 0);
        CHECK(!normalizeMac("0A:11:22:00:00", m));       // too short
        CHECK(!normalizeMac("zz:11:22:00:00:21", m));    // non-hex
        CHECK(!normalizeMac("0A:11:22:00:00:21:33", m)); // too long
    }

    // Inventory vendor matches the OUI registry for every genuine device.
    for (int i = 0; i < assetCount(); i++) {
        const Asset& a = asset(i);
        CHECK(std::strcmp(vendorForMac(a.mac), a.vendor) == 0);
    }

    // The impostor announces a trusted station name but its MAC is NOT that station's inventory MAC,
    // and its OUI is attributed to an untrusted vendor.
    {
        const Asset* spoofed = assetByName(takeover::spoofedName());
        CHECK(spoofed != nullptr);
        CHECK(std::strcmp(takeover::impostorMac(), spoofed->mac) != 0); // different MAC, same name
        const char* v = vendorForMac(takeover::impostorMac());
        CHECK(v != nullptr && std::strcmp(v, spoofed->vendor) != 0);    // OUI != inventory vendor
        // The impostor MAC + name actually appear together in the saved capture.
        bool found = false;
        for (int i = 0; i < dcpCount(); i++) {
            const DcpFrame& f = dcp(i);
            if (std::strcmp(f.srcMac, takeover::impostorMac()) == 0 &&
                std::strcmp(f.deviceName, takeover::spoofedName()) == 0) found = true;
        }
        CHECK(found);
    }

    // The historian access log still carries exactly one external export - background evidence now;
    // it is no longer the #07 answer (which is the SQL-injection flag).
    {
        int ext = 0;
        for (int i = 0; i < accessLogCount(); i++) if (accessLog(i).external) ext++;
        CHECK(ext == 1);
    }

    // ----- #07 Historian SQL Injection: submit the flag extracted via SQLi -----
    {
        forensics::State s;
        CHECK(!s.solved());
        CHECK(!s.submit("flag{wrong}"));                   // wrong flag -> no solve, no points
        CHECK(!s.submit("REACTOR.SP"));                    // the OLD tag/value path no longer completes #07
        CHECK(!s.submit(""));                              // empty
        CHECK(!s.submit(nullptr));                         // null-safe
        CHECK(!s.solved());                                // no premature solve
        CHECK(s.submit(FORENSICS_FLAG));                   // the correct SQLi flag
        CHECK(s.solved());
        CHECK(std::strcmp(s.flag(), FORENSICS_FLAG) == 0);
        CHECK(s.submit(FORENSICS_FLAG) && s.solved());     // idempotent: a repeat stays solved (points once)
        s.reset(); CHECK(!s.solved());                     // reset + repeatable
        CHECK(s.submit(FORENSICS_FLAG) && s.solved());
    }

    // Single source of truth: the #07 flag the submission validates against IS exactly the flag the
    // SQLi simulator emits from its `flags` table (registry/simulator/handler cannot drift).
    CHECK(std::strcmp(FORENSICS_FLAG, ot::sim::histsqlunion::FLAG) == 0);

    // ----- #09 Trojan Historian: restore + hold -----
    {
        // live trend has a gap exactly at MISSING_SEQ; backup fills it.
        bool liveHasGap = true, backupHas = false;
        for (int i = 0; i < trojan::liveCount(); i++) if (trojan::live(i).seq == trojan::MISSING_SEQ) liveHasGap = false;
        for (int i = 0; i < trojan::backupCount(); i++)
            if (trojan::backup(i).seq == trojan::MISSING_SEQ && trojan::backup(i).value == trojan::MISSING_VALUE) backupHas = true;
        CHECK(liveHasGap && backupHas);

        trojan::State s; s.reset(0);
        CHECK(!s.restore(trojan::MISSING_SEQ, 700, 0));   // wrong value -> not restored
        CHECK(!s.restored());
        s.tick(20000); CHECK(!s.solved());                // wrong restore never solves
        CHECK(!s.restore(99, trojan::MISSING_VALUE, 20000)); // wrong seq
        CHECK(s.restore(trojan::MISSING_SEQ, trojan::MISSING_VALUE, 30000)); // correct
        CHECK(s.restored() && !s.solved());               // not solved instantly
        s.tick(35000); CHECK(!s.solved());                // 5s held
        s.tick(40000); CHECK(s.solved());                 // full HOLD_MS -> solved
        CHECK(std::strcmp(s.flag(), TROJAN_FLAG) == 0);
    }
    {
        // a wrong overwrite after a correct restore re-corrupts and clears the hold timer
        trojan::State s; s.reset(0);
        s.restore(trojan::MISSING_SEQ, trojan::MISSING_VALUE, 0);
        s.tick(5000); CHECK(!s.solved());
        CHECK(!s.restore(trojan::MISSING_SEQ, 700, 5000)); // corrupt again
        CHECK(!s.restored());
        s.tick(20000); CHECK(!s.solved());                 // timer was cleared
        // re-solve works
        s.restore(trojan::MISSING_SEQ, trojan::MISSING_VALUE, 20000);
        s.tick(30000); CHECK(s.solved());
    }
    {
        // B2: holdRemainingMs gives honest progress feedback during the stability hold.
        trojan::State s; s.reset(0);
        CHECK(s.holdRemainingMs(0) == 0);                                  // not holding yet
        s.restore(trojan::MISSING_SEQ, trojan::MISSING_VALUE, 1000);       // correct -> holding
        CHECK(s.holdRemainingMs(1000) == trojan::HOLD_MS);                 // full hold remaining
        CHECK(s.holdRemainingMs(1000 + 4000) == trojan::HOLD_MS - 4000);   // counts down
        CHECK(s.holdRemainingMs(1000 + trojan::HOLD_MS + 50) == 0);        // clamps at 0
        s.tick(1000 + trojan::HOLD_MS);                                    // complete the hold
        CHECK(s.solved());
        CHECK(s.holdRemainingMs(1000 + trojan::HOLD_MS) == 0);            // 0 once solved
        // a wrong overwrite clears the hold -> remaining 0 again
        trojan::State w; w.reset(0);
        w.restore(trojan::MISSING_SEQ, trojan::MISSING_VALUE, 0);
        CHECK(w.holdRemainingMs(100) == trojan::HOLD_MS - 100);
        w.restore(trojan::MISSING_SEQ, 700, 100);                         // corrupt
        CHECK(w.holdRemainingMs(100) == 0);
    }

    // ----- #10 Device Takeover: submit MAC -----
    {
        takeover::State s;
        CHECK(!s.submit("00:00:00:00:00:00"));            // unrelated
        CHECK(!s.submit("0A:11:22:00:00:21"));            // the GENUINE PLC MAC is not the impostor
        CHECK(!s.solved());                               // no premature solve
        CHECK(!s.submit("not-a-mac"));                    // malformed
        CHECK(s.submit("0a-77-88-00-00-aa"));             // impostor, lower + dashes
        CHECK(s.solved());
        CHECK(std::strcmp(s.flag(), DEVICE_FLAG) == 0);
        s.reset(); CHECK(!s.solved());                    // reset + repeatable
        CHECK(s.submit(takeover::impostorMac()) && s.solved());
    }

    // ----- cross-challenge isolation: independent State objects don't share solve state -----
    {
        forensics::State f; trojan::State t; takeover::State d; t.reset(0);
        f.submit(FORENSICS_FLAG);
        CHECK(f.solved() && !t.solved() && !d.solved());
    }

    // ----- threaded webmail: every evidence file attached to the right conversation (Evidence Catalog),
    //       plus the new #04 pump thread, the historian split (:8443) and the OPC move off 4840. -----
    {
        auto hasMsg = [](const char* from, const char* attach) -> bool {
            for (int i = 0; i < threadCount(); i++) {
                const MailThread& t = thread(i);
                for (int m = 0; m < t.msgCount; m++) {
                    const MailMsg& e = t.msgs[m];
                    if (std::strcmp(e.from, from) == 0 && e.attachment && std::strcmp(e.attachment, attach) == 0) return true;
                }
            }
            return false;
        };
        auto anyBody = [](const char* needle) -> bool {
            for (int i = 0; i < threadCount(); i++) {
                const MailThread& t = thread(i);
                for (int m = 0; m < t.msgCount; m++) if (std::strstr(t.msgs[m].body, needle)) return true;
            }
            return false;
        };
        auto fromBodyLacks = [](const char* from, const char* needle) {
            for (int i = 0; i < threadCount(); i++) {
                const MailThread& t = thread(i);
                for (int m = 0; m < t.msgCount; m++)
                    if (std::strcmp(t.msgs[m].from, from) == 0) CHECK(!std::strstr(t.msgs[m].body, needle));
            }
        };
        CHECK(hasMsg("k.mensah@sysrupt.local", "rtu560_points"));     // #01
        CHECK(hasMsg("s.okafor@sysrupt.local", "sis_maint_note"));    // #02
        CHECK(hasMsg("protection@sysrupt.local", "goose_monitor"));   // #06
        CHECK(hasMsg("fieldops@sysrupt.local", "rtu07_recovery"));    // #08
        CHECK(hasMsg("controls@sysrupt.local", "reactor_io_map"));    // #11
        CHECK(hasMsg("soc@sysrupt.local", "historian_access_log"));   // #07 (D2)
        CHECK(hasMsg("eng@sysrupt.local", "historian_backup"));       // #09 (D4)
        CHECK(hasMsg("itsec@sysrupt.local", "dcp_capture"));          // #10 (D5)
        CHECK(hasMsg("it@sysrupt.local", "asset_inventory"));         // #10 reference
        CHECK(hasMsg("it@sysrupt.local", "oui_registry"));            // #10 reference (both files homed in it@ thread)
        // #04 Rogue Radio: the pump capture now has its own mail thread (standalone Evidence page gone).
        bool pump = false;
        for (int i = 0; i < threadCount(); i++) {
            const MailThread& t = thread(i);
            for (int m = 0; m < t.msgCount; m++)
                if (t.msgs[m].attachment && std::strcmp(t.msgs[m].attachment, "pump_capture") == 0) pump = true;
        }
        CHECK(pump);
        // #03 OPC thread: names the server ident + the self-rolled lite port, and NO longer advertises 4840.
        bool opcIdent = false, opcPort = false;
        for (int i = 0; i < threadCount(); i++) {
            const MailThread& t = thread(i);
            for (int m = 0; m < t.msgCount; m++) if (std::strcmp(t.msgs[m].from, "itops@sysrupt.local") == 0) {
                if (std::strstr(t.msgs[m].body, "Sysrupt-OPCUA")) opcIdent = true;
                if (std::strstr(t.msgs[m].body, "9005")) opcPort = true;
            }
        }
        CHECK(opcIdent && opcPort);
        fromBodyLacks("itops@sysrupt.local", "4840");                 // no standard OPC UA port anymore
        // the realigned #01 thread must NOT describe the fictional alarm-suppressing maintenance window.
        fromBodyLacks("k.mensah@sysrupt.local", "maintenance/test window");
        // the historian is referenced as a separate internal system (its /hist path is discovered via
        // robots.txt -> /hist-config, not spelled out in mail, so #07's console-location hint keeps value).
        CHECK(anyBody("separate internal system"));
    }

    // ----- engineering reference docs stay numerically consistent with the simulator constants -----
    // (so the player-facing evidence can never silently drift from the code it documents).
    {
        char b[24];
        // #01 - rtu560_points vs ot::sub
        const char* d1 = rtu560PointsDoc();
        std::snprintf(b, sizeof b, "IOA %u", (unsigned)ot::sub::IOA_BREAKER);        CHECK(std::strstr(d1, b)); // 1001
        std::snprintf(b, sizeof b, "IOA %u", (unsigned)ot::sub::IOA_TEST_BREAKER);   CHECK(std::strstr(d1, b)); // 1000
        std::snprintf(b, sizeof b, "IOA %u", (unsigned)ot::sub::IOA_LAST_LEGIT_ORIG);CHECK(std::strstr(d1, b)); // 2003
        std::snprintf(b, sizeof b, "IOA %u", (unsigned)ot::sub::IOA_INTERROG_LOCK);  CHECK(std::strstr(d1, b)); // 2001
        std::snprintf(b, sizeof b, "IOA %u", (unsigned)ot::sub::IOA_FLAG_BASE);      CHECK(std::strstr(d1, b)); // 5000
        std::snprintf(b, sizeof b, "#%u", (unsigned)ot::sub::SUBSTATION_ORIG);       CHECK(std::strstr(d1, b)); // #7
        CHECK(ot::sub::COMMON_ADDR == 1 && std::strstr(d1, "(CA) = 1"));
        CHECK(std::strstr(d1, "C_SC") && std::strstr(d1, "45") && std::strstr(d1, "COT 6") && std::strstr(d1, "SCS=0"));

        // #02 - sis_maint_note vs ot::sis (the rolling-key register stays hidden / found by fuzzing)
        const char* d2 = sisMaintDoc();
        std::snprintf(b, sizeof b, "reg %u", (unsigned)ot::sis::REG_TRIP);           CHECK(std::strstr(d2, b)); // 0
        std::snprintf(b, sizeof b, "reg %u", (unsigned)ot::sis::REG_LOCK);           CHECK(std::strstr(d2, b)); // 1
        std::snprintf(b, sizeof b, "reg %u", (unsigned)ot::sis::REG_FW_COMPAT);      CHECK(std::strstr(d2, b)); // 2
        std::snprintf(b, sizeof b, "reg %u", (unsigned)ot::sis::REG_CHECKSUM_MATCH); CHECK(std::strstr(d2, b)); // 4
        std::snprintf(b, sizeof b, "reg %u", (unsigned)ot::sis::REG_LOCK_EXPIRY);    CHECK(std::strstr(d2, b)); // 5
        CHECK(std::strstr(d2, "1502") && std::strstr(d2, "0xAA 0x55"));
        CHECK(std::strstr(d2, "0x01") && std::strstr(d2, "0x02") && std::strstr(d2, "0x10"));
        std::snprintf(b, sizeof b, "reg %u", (unsigned)ot::sis::REG_DEBUG_KEY);      CHECK(!std::strstr(d2, b)); // 42 hidden

        // #06 - goose_monitor vs ot::goose
        const char* d3 = gooseMonitorDoc();
        std::snprintf(b, sizeof b, "0x%04X", (unsigned)ot::goose::EXPECTED_APPID);   CHECK(std::strstr(d3, b)); // 0x0BB8
        std::snprintf(b, sizeof b, "%u", (unsigned)ot::goose::UDP_PORT);             CHECK(std::strstr(d3, b)); // 6180
        std::snprintf(b, sizeof b, "confRev): %u", (unsigned)ot::goose::EXPECTED_CONFREV); CHECK(std::strstr(d3, b)); // 1
        CHECK(std::strstr(d3, "true = OPEN") && std::strstr(d3, "stNum"));

        // #08 - rtu07_recovery vs ot::snmp (write community + last-good value stay OFF the doc -> read live)
        const char* d4 = rtu07RecoveryDoc();
        std::snprintf(b, sizeof b, "%u", (unsigned)ot::snmp::UDP_PORT);              CHECK(std::strstr(d4, b)); // 161
        CHECK(std::strstr(d4, ot::snmp::READ_COMMUNITY));                                                       // public
        CHECK(std::strstr(d4, "42424") && std::strstr(d4, ".1.0") && std::strstr(d4, ".4.0") &&
              std::strstr(d4, ".9.0") && std::strstr(d4, "sysContact"));
        CHECK(!std::strstr(d4, ot::snmp::WRITE_COMMUNITY));                                                     // r3c0v3ry-07 not handed
        std::snprintf(b, sizeof b, "%d", (int)ot::snmp::LAST_GOOD_IMAGE);            CHECK(!std::strstr(d4, b)); // 341 not handed

        // #11 - reactor_io_map vs ot::reg / ot::cfg (control + sensor map, safe band, SCRAM, hold).
        // This is the player-facing discoverability guarantee for Stabilise the Reactor: the control
        // register addresses, the IR5 bit meanings, the safe-band envelope, the SCRAM recovery and the
        // flag block are all documented, and they can never drift from the simulator constants.
        const char* d5 = reactorIoMapDoc();
        std::snprintf(b, sizeof b, "HR%u", (unsigned)ot::reg::HR_ROD_POSITION);   CHECK(std::strstr(d5, b)); // HR0
        std::snprintf(b, sizeof b, "HR%u", (unsigned)ot::reg::HR_WATER_FLOW);     CHECK(std::strstr(d5, b)); // HR1
        std::snprintf(b, sizeof b, "HR%u", (unsigned)ot::reg::HR_PRESSURE_VALVE); CHECK(std::strstr(d5, b)); // HR2
        std::snprintf(b, sizeof b, "IR%u", (unsigned)ot::reg::IR_CORE_TEMP);      CHECK(std::strstr(d5, b)); // IR1
        std::snprintf(b, sizeof b, "IR%u", (unsigned)ot::reg::IR_SAFE_BAND);      CHECK(std::strstr(d5, b)); // IR5
        std::snprintf(b, sizeof b, "IR%u", (unsigned)ot::reg::IR_PLANT_STATE);    CHECK(std::strstr(d5, b)); // IR6
        std::snprintf(b, sizeof b, "IR%u", (unsigned)ot::reg::IR_CH1_FLAG);       CHECK(std::strstr(d5, b)); // IR32
        std::snprintf(b, sizeof b, "%d", (int)ot::cfg::TEMP_SAFE_LO);     CHECK(std::strstr(d5, b)); // 2900
        std::snprintf(b, sizeof b, "%d", (int)ot::cfg::TEMP_SAFE_HI);     CHECK(std::strstr(d5, b)); // 3300
        std::snprintf(b, sizeof b, "%d", (int)ot::cfg::PRESSURE_SAFE_LO); CHECK(std::strstr(d5, b)); // 600
        std::snprintf(b, sizeof b, "%d", (int)ot::cfg::PRESSURE_SAFE_HI); CHECK(std::strstr(d5, b)); // 1050
        std::snprintf(b, sizeof b, "%d", (int)ot::cfg::FLUX_SAFE_HI);     CHECK(std::strstr(d5, b)); // 650
        std::snprintf(b, sizeof b, "%d", (int)ot::cfg::SCRAM_TEMP);       CHECK(std::strstr(d5, b)); // 3800
        CHECK(std::strstr(d5, "bit0") && std::strstr(d5, "bit1") && std::strstr(d5, "bit2")); // mask bit meanings
        CHECK(std::strstr(d5, "IR5 = 7") && std::strstr(d5, "5 s"));                           // success + hold
        CHECK(std::strstr(d5, "SCRAM") && std::strstr(d5, "re-stabilise"));                    // recovery documented
        CHECK(!std::strstr(d5, ot::cfg::CH1_FLAG));                                            // never leak the flag
    }

    printf("defensive tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
