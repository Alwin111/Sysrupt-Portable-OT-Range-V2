// SPDX-License-Identifier: Apache-2.0
#include "Defensive.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ot::def {

namespace {

// ----- device identities (PROVISIONAL; locally administered MACs, registry-backed OUIs) ----------
// The impostor (DCP) announces PLC-01's name + IP from OUI 0A:77:88, which the inventory never
// registers to PLC-01 and the OUI registry attributes to an untrusted vendor.
constexpr const char* GENUINE_PLC_MAC = "0A:11:22:00:00:21";
constexpr const char* IMPOSTOR_MAC    = "0A:77:88:00:00:AA";
constexpr const char* IMPOSTOR_TS     = "2026-03-14T02:17:09Z";
constexpr const char* SPOOFED_NAME    = "PLC-01";

constexpr Asset ASSETS[] = {
    { "PLC-01",  "Reactor control PLC", "10.0.1.21", "0A:11:22:00:00:21", "Sysrupt Controls", "fw 3.4.1" },
    { "RTU-02",  "Substation RTU",      "10.0.1.22", "0A:11:22:00:00:22", "Sysrupt Controls", "fw 2.1.0" },
    { "HMI-03",  "Operator HMI",        "10.0.1.23", "0A:33:44:00:00:23", "Northfield HMI",    "fw 5.0.2" },
    { "HIST-04", "Process historian",   "10.0.1.24", "0A:55:66:00:00:24", "Keystone Data",     "fw 1.9.7" },
    // RTU-07: field RTU flagged unhealthy after the incident (#08 Field Device Recovery). Listed with a
    // non-trusted "unresponsive" status so its management address is discoverable here as well as in the
    // fieldops@ mail thread. It answers SNMP on 10.0.1.1:161 (the shared range AP address).
    { "RTU-07",  "Field RTU (unresponsive)", "10.0.1.1", "0A:11:22:00:00:27", "Sysrupt Controls", "fw 3.1.0" },
};
constexpr int ASSET_COUNT = (int)(sizeof(ASSETS) / sizeof(ASSETS[0]));

constexpr OuiEntry OUIS[] = {
    { "0A:11:22", "Sysrupt Controls" },
    { "0A:33:44", "Northfield HMI" },
    { "0A:55:66", "Keystone Data" },
    { "0A:77:88", "Riverside Instruments (not a registered substation vendor)" },
};
constexpr int OUI_COUNT = (int)(sizeof(OUIS) / sizeof(OUIS[0]));

// Saved PROFINET DCP capture (deterministic). Frame 4 is the impostor: it claims PLC-01 + 10.0.1.21
// but from IMPOSTOR_MAC, after the genuine PLC-01 announced itself from GENUINE_PLC_MAC in frame 1.
constexpr DcpFrame DCP[] = {
    { "2026-03-14T02:17:03Z", "0A:11:22:00:00:21", "PLC-01",  "10.0.1.21", "Hello" },
    { "2026-03-14T02:17:04Z", "0A:33:44:00:00:23", "HMI-03",  "10.0.1.23", "Hello" },
    { "2026-03-14T02:17:05Z", "0A:55:66:00:00:24", "HIST-04", "10.0.1.24", "Hello" },
    { "2026-03-14T02:17:09Z", "0A:77:88:00:00:AA", "PLC-01",  "10.0.1.21", "Ident" },
};
constexpr int DCP_COUNT = (int)(sizeof(DCP) / sizeof(DCP[0]));

// Historian access log (for Historian Forensics). 203.0.113.9 is an RFC 5737 documentation address
// (external to the 10.0.1.0/24 engineering subnet): it exports REACTOR.SP, the tampered tag.
constexpr AccessLog ACCESS[] = {
    { "2026-03-14T01:55:12Z", "eng01",   "10.0.1.50",   "READ",   "REACTOR.TEMP", false },
    { "2026-03-14T01:58:40Z", "eng01",   "10.0.1.50",   "READ",   "REACTOR.SP",   false },
    { "2026-03-14T02:03:27Z", "svc_hist","10.0.1.24",   "WRITE",  "REACTOR.SP",   false },
    { "2026-03-14T02:11:55Z", "UNKNOWN", "203.0.113.9", "EXPORT", "REACTOR.SP",   true  },
    { "2026-03-14T02:19:02Z", "eng01",   "10.0.1.50",   "READ",   "PUMP.FLOW",    false },
};
constexpr int ACCESS_COUNT = (int)(sizeof(ACCESS) / sizeof(ACCESS[0]));

// Seeded webmail, modelled as threaded conversations. Each thread opens with the original report and
// may carry a realistic follow-up; evidence is attached to the message that references it (there is no
// separate evidence page). A follow-up can introduce a detail missing from the opening message (an
// address, a correction, a nudge), so some clues reveal themselves through reading the conversation.
// The thread -> evidence -> challenge mapping follows the correction write-up's Evidence Catalog.

// #07 Historian Forensics (D2): external export of the reactor setpoint tag. The follow-up gives the
// historian console's own address (a separate system) and the "search behaves oddly" SQLi tell.
constexpr MailMsg MSG_SOC[] = {
    { "soc@sysrupt.local", "2026-03-14T02:25:00Z",
      "SOC flagged an EXPORT of a reactor tag from an external address in the historian access log "
      "(attached). Identify which tag was pulled externally and recover its last trustworthy value "
      "before the tampering. - SOC",
      "historian_access_log" },
    { "soc@sysrupt.local", "2026-03-14T02:41:00Z",
      "Note: the process historian (HIST-04) isn't on this portal - it's a separate internal system "
      "with its own console at 10.0.1.1/hist and its own read-only login (the default commissioning "
      "credential is reportedly still in its service config). The access log is from there. One of the "
      "analysts said the tag-search field there behaves strangely when you put punctuation in it - worth "
      "a look while you're in. - SOC",
      nullptr },
};

// #09 Trojan Historian (D4): deleted TURBINE.VIB sample; the backup still holds it.
constexpr MailMsg MSG_ENG[] = {
    { "eng@sysrupt.local", "2026-03-14T02:28:00Z",
      "A single sample is missing from the TURBINE.VIB live trend - looks like a deletion. The nightly "
      "backup still has it (attached). Restore the missing record and confirm it stays stable. - Eng",
      "historian_backup" },
    { "eng@sysrupt.local", "2026-03-14T02:44:00Z",
      "The backup's attached; restore the missing record via the portal SUBMIT form and keep it stable "
      "for a bit so the trend reconciles - a wrong value just re-corrupts it. (The historian's own "
      "read-only console is a separate system at 10.0.1.1/hist.) - Eng",
      nullptr },
};

// #10 Device Takeover (D5): a DCP impostor announces a trusted station's name from an untrusted OUI.
constexpr MailMsg MSG_ITSEC[] = {
    { "itsec@sysrupt.local", "2026-03-14T02:31:00Z",
      "We captured PROFINET DCP announcements overnight (attached). One station name is claimed by two "
      "different MACs. Find the impostor's MAC and confirm it against the asset inventory and OUI "
      "registry. - IT Security",
      "dcp_capture" },
};

// #10 reference material: the trusted asset inventory + the OUI registry (two attachments, so the
// second rides a follow-up message).
constexpr MailMsg MSG_IT[] = {
    { "it@sysrupt.local", "2026-03-14T02:33:00Z",
      "Reference for the identity work: the engineering asset inventory is attached, and it's also on "
      "the portal under INVENTORY. Use it to validate device identities. - IT",
      "asset_inventory" },
    { "it@sysrupt.local", "2026-03-14T02:34:00Z",
      "And the vendor OUI registry (MAC-prefix -> vendor) for this plant's device ranges, attached. One "
      "prefix in there is NOT a registered substation vendor - watch for it. - IT",
      "oui_registry" },
};

// #01 Tripping Breaker (O1): the engineer's complaint + archived RTU560 point list. (Unchanged wording;
// it deliberately omits the exact IOA numbers/originator/command fields so the live wire does the work.)
constexpr MailMsg MSG_IEC[] = {
    { "k.mensah@sysrupt.local", "2026-03-14T01:40:00Z",
      "Flagging this again: the Feeder-1 breaker IED is an ABB RTU560 - IEC-104 on 2404, no auth. A "
      "general interrogation (QOI 20) enumerates the full point list (archived export attached). The "
      "RTU only accepts a breaker command carrying the standing switching originator, logs it against "
      "that originator, and silently drops commands during its general-interrogation refresh. A "
      "decommissioned test-bay breaker is also still mapped and just swallows commands. Gotcha: the RTU "
      "ACKs any command it accepts - that is NOT proof the breaker moved, so re-check the breaker "
      "position and get the open/close state right. Needs IEC 62351. - K. Mensah, Substation I&C",
      "rtu560_points" },
};

// #03 OPC Scout (O3): routes to the monitoring server. The follow-up is the in-world signal that this
// is a self-rolled "OPC-UA-lite" service, not a standards-compliant OPC UA server (so a standard client
// will not complete a handshake) - the port is finalised with the service move off 4840.
constexpr MailMsg MSG_OPC[] = {
    { "itops@sysrupt.local", "2026-03-14T01:50:00Z",
      "Heads up on the plant monitoring server (server ident 'Sysrupt-OPCUA/1.0'). After the last "
      "reconfig a node was unlinked from the Objects tree but is still readable - the Diagnostics node "
      "under Server still references its NodeId. Browse from Root (Objects / Server) and check "
      "Server > Diagnostics. - IT Ops",
      nullptr },
    { "itops@sysrupt.local", "2026-03-14T01:58:00Z",
      "Important: despite the 'OPC' name this is NOT a standards-compliant OPC UA server - it's an "
      "in-house OPC-UA-lite service on a non-standard port (10.0.1.1:9005), so a normal OPC UA client "
      "(UA Expert / python-opcua) will not complete a handshake with it. Use the lite framing from the "
      "monitoring tooling. - IT Ops",
      nullptr },
};

// #02 Safety Instrumented Bypass (O2): SIS maintenance interface + TriStation-lite note. The follow-up
// replaces what the note used to hand over: a direct trip write is refused and a firmware-only download
// is rejected too, so the download path clearly wants another value (learned from the error responses).
constexpr MailMsg MSG_SIS[] = {
    { "s.okafor@sysrupt.local", "2026-03-14T01:30:00Z",
      "The safety controller's legacy TriStation-lite maintenance interface is still reachable on TCP "
      "1502 with no authentication. Internal tech note on the framing + register map attached. A direct "
      "write to the trip register is refused - program changes go through the download path documented "
      "in the note. We need IEC 62443 segmentation here before someone abuses it. - S. Okafor, "
      "Safety I&C",
      "sis_maint_note" },
    { "s.okafor@sysrupt.local", "2026-03-14T01:47:00Z",
      "Confirmed on the bench: a direct write to the trip just comes back refused, and a PROGRAM-DOWNLOAD "
      "with only the firmware is rejected too - the controller clearly wants something else in the "
      "download that isn't in our note. Whoever reviews this will need to find it. - S. Okafor",
      nullptr },
};

// #06 Second Blackout (O6): GOOSE training monitor + config note. The follow-up tells the player WHERE
// the accepted breaker state is shown (the IR intranet home panel), not the reactor HMI.
constexpr MailMsg MSG_GOOSE[] = {
    { "protection@sysrupt.local", "2026-03-14T01:35:00Z",
      "The protection training monitor is publishing GOOSE on UDP 6180 (training encapsulation, not "
      "L2). Config note attached - subscribed APPID, confRev and the modelled dataset. Reminder that "
      "our relays trust any newer state number on the subscribed APPID with no authentication; worth "
      "demonstrating at the next tabletop. - Protection",
      "goose_monitor" },
    { "protection@sysrupt.local", "2026-03-14T01:53:00Z",
      "If you're testing it: the breaker position the subscriber actually believes is shown on the IR "
      "portal HOME page (the bay GOOSE subscriber panel), not on the reactor HMI - that's the place to "
      "confirm whether an injected frame took effect. - Protection",
      nullptr },
};

// #04 Rogue Radio / Custom Pump Protocol (O4): NEW thread homing the pump-bus capture (the Evidence
// page is gone). The follow-up carries the status/flag-block read-back detail.
constexpr MailMsg MSG_PUMP[] = {
    { "r.ferreira@sysrupt.local", "2026-03-14T01:12:00Z",
      "The flow-control pump bus still runs over the old radio gateway - a custom binary protocol on "
      "TCP 10.0.1.1:7700, no auth. I saved a capture of normal traffic (attached): it shows the frame "
      "format, the checksum rule, which feeder is the real flow valve versus the decoys, and the "
      "anti-replay. A byte-for-byte replay is rejected; you'd have to craft a fresh, higher-sequence "
      "SELECT then OPEN. - R. Ferreira, Pump & Water I&C",
      "pump_capture" },
    { "r.ferreira@sysrupt.local", "2026-03-14T01:21:00Z",
      "One more thing from the capture: the gateway's status/flag block reads back on the SAME socket "
      "with action byte 05 (target = word index), same frame shape as a command. It reads zero until "
      "the target valve has actually been selected, opened and held open. - R. Ferreira",
      nullptr },
};

// #08 Field Device Recovery (D3): RTU-07 SNMP recovery runbook. The IP is stated outright (this is a
// 'what to do once connected' challenge, not address discovery) and RTU-07 is also in the inventory.
constexpr MailMsg MSG_FIELDOPS[] = {
    { "fieldops@sysrupt.local", "2026-03-14T02:05:00Z",
      "Field RTU-07 (SNMP agent on 10.0.1.1:161, read community public) is flagged unhealthy after the "
      "incident - its running firmware image is not the last-good image. Use the recovery runbook "
      "attached to restore it to its own last-good image over SNMP and confirm it reads healthy again. "
      "The write community is in the device's sysContact. It's also in the asset inventory now, marked "
      "unresponsive. - Field Ops",
      "rtu07_recovery" },
};

// #11 Stabilise the Reactor (O-series): SCADA I/O map / commissioning worksheet.
constexpr MailMsg MSG_CONTROLS[] = {
    { "controls@sysrupt.local", "2026-03-14T01:20:00Z",
      "Unit-1 is sitting in an unsafe start-up state (rod withdrawn, weak cooling) and the core "
      "temperature is climbing. Control is over Modbus/TCP 10.0.1.1:502. I attached the SCADA I/O "
      "map / commissioning worksheet: it has the control + sensor register map, the safe-band mask "
      "and the operating envelope. Mind the over-temperature SCRAM - if it trips, wait for it to "
      "clear and re-stabilise. - Controls Eng",
      "reactor_io_map" },
};

#define MAIL_THREAD(subj, arr) { subj, arr, (int)(sizeof(arr) / sizeof(arr[0])) }
constexpr MailThread THREADS[] = {
    MAIL_THREAD("External export of reactor setpoint tag", MSG_SOC),
    MAIL_THREAD("Trend gap on TURBINE.VIB", MSG_ENG),
    MAIL_THREAD("Suspicious PROFINET DCP activity on the cell network", MSG_ITSEC),
    MAIL_THREAD("Asset inventory + OUI registry (reference)", MSG_IT),
    MAIL_THREAD("Feeder-1 breaker RTU - still no authentication", MSG_IEC),
    MAIL_THREAD("Monitoring server - orphaned diagnostics node", MSG_OPC),
    MAIL_THREAD("SIS maintenance port still exposed on :1502", MSG_SIS),
    MAIL_THREAD("GOOSE training monitor on the bay LAN (UDP 6180)", MSG_GOOSE),
    MAIL_THREAD("Pump-bus radio gateway on :7700 - saved capture", MSG_PUMP),
    MAIL_THREAD("RTU-07 running a tampered image - recover via SNMP", MSG_FIELDOPS),
    MAIL_THREAD("Unit-1 reactor start-up - stabilisation worksheet", MSG_CONTROLS),
};
#undef MAIL_THREAD
constexpr int THREAD_COUNT = (int)(sizeof(THREADS) / sizeof(THREADS[0]));

bool ieq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

} // namespace

// ----- evidence accessors -------------------------------------------------------------------------
int threadCount() { return THREAD_COUNT; }
const MailThread& thread(int i) { return THREADS[i]; }
int mailMsgCount() { int n = 0; for (auto& t : THREADS) n += t.msgCount; return n; }

int assetCount() { return ASSET_COUNT; }
const Asset& asset(int i) { return ASSETS[i]; }
const Asset* assetByName(const char* name) {
    for (auto& a : ASSETS) if (ieq(a.name, name)) return &a;
    return nullptr;
}

int ouiCount() { return OUI_COUNT; }
const OuiEntry& oui(int i) { return OUIS[i]; }

int dcpCount() { return DCP_COUNT; }
const DcpFrame& dcp(int i) { return DCP[i]; }

int accessLogCount() { return ACCESS_COUNT; }
const AccessLog& accessLog(int i) { return ACCESS[i]; }

// ----- engineering reference documents (rendered verbatim by net/DefWeb at /evidence?f=<name>) -----
// Every concrete value below is cross-checked against the matching simulator constant by the host
// tests (ot_defensive_tests), so a point/register/OID number here cannot silently diverge from code.
// These read like realistic archived OT documents: they make the solve DERIVABLE (not a walkthrough),
// leaving the live wire to supply the current values (originator, firmware word, last-good image, ...)
// and the player to craft the actual frames, time the windows and reverse the flag encodings.

// #01 IEC-104 RTU560 - mirrors ot::sub constants (CA 1; IOA 1000/1001/2001/2002/2003/2004; flag 5000;
// C_SC type 45, COT 6, SCS polarity; SUBSTATION_ORIG 7).
const char* rtu560PointsDoc() {
    return
"# ABB RTU560 - Feeder-1 substation bay - engineering point list (archived config export)\n"
"# Protocol: IEC 60870-5-104, TCP 2404. ASDU common address (CA) = 1. No IEC 62351 auth (OPEN FINDING).\n"
"\n"
"[ breaker control group ]\n"
"IOA 1000  CB-TEST   M_SP   test-bay breaker; bay decommissioned 2024, point left mapped for a legacy\n"
"                           poll ONLY - not wired to primary plant (a command is confirmed but has no\n"
"                           field effect and raises no alarm)\n"
"IOA 1001  FDR1-CB   M_SP   Feeder-1 MAIN breaker (primary plant).  0 = closed/energised, 1 = open\n"
"\n"
"[ audit / originator group ]\n"
"IOA 2001  INTERROG_ACTIVE  M_SP  1 while a periodic general-interrogation refresh is running; the RTU\n"
"                                 silently DROPS control commands for the duration of that window\n"
"IOA 2002  LAST_TRIP_ORIG   M_ME  originator address recorded for the last accepted breaker command\n"
"IOA 2003  LAST_LEGIT_ORIG  M_ME  standing switching-authority originator. The RTU accepts a breaker\n"
"                                 command ONLY if it carries this originator, and stamps the action\n"
"                                 against it in the audit trail\n"
"IOA 2004  TRIP_CONFIRMED   M_SP  set once a breaker trip has been accepted\n"
"\n"
"[ vendor data block ]\n"
"IOA 5000-5015  M_ME  READ-ONLY vendor ASCII block, two characters per object (little-endian on the\n"
"                     wire, so swap each pair to read text). Not writable; stays all-zero until the\n"
"                     bay's switching operation is completed and confirmed, then the RTU populates it.\n"
"\n"
"[ command reference ]\n"
"Breaker switching uses a single command (C_SC, type id 45) with cause-of-transmission ACTIVATION\n"
"(COT 6). The single command state (SCS) selects the position: SCS=0 (OFF) opens/trips the breaker,\n"
"SCS=1 (ON) closes it.\n"
"The RTU returns an activation-confirmation (ACTCON) for every command it ACCEPTS - including a close,\n"
"or a command to the decommissioned test bay - so a positive confirmation is NOT proof the Feeder-1\n"
"breaker moved. After an accepted command the RTU spontaneously reports the resulting Feeder-1 position\n"
"on IOA 1001: a real trip shows IOA 1001 = 1 (open) and sets TRIP_CONFIRMED (IOA 2004); a command that\n"
"did not open it leaves IOA 1001 = 0. Confirm the breaker POSITION (re-interrogate 1001 / 2004), not\n"
"just the activation-confirmation.\n"
"\n"
"[ maintenance record - 2026-02 ]\n"
"All Feeder-1 switching is performed under I&C originator #7 (K. Mensah). Pending action: deploy IEC\n"
"62351 so the breaker can no longer be operated by anyone able to assert that originator address.\n";
}

// #02 TriStation-lite SIS - mirrors ot::sis constants (port 1502; REG_TRIP 0 / REG_LOCK 1 /
// REG_FW_COMPAT 2 / REG_LAST_CHECKSUM 3 / REG_CHECKSUM_MATCH 4 / REG_LOCK_EXPIRY 5; REG_FLAG_BASE 100).
// The rolling key register (42) is intentionally NOT named - it is found by scanning the space.
const char* sisMaintDoc() {
    return
"# Safety controller (Triconex-class SIS) - maintenance interface - internal tech note\n"
"# Port: TCP 1502 (TriStation-lite). No authentication (IEC 62443 gap - flagged).\n"
"\n"
"[ framing ]  every message: sync 0xAA 0x55, then a command byte, then big-endian arguments.\n"
"  0x01  READ   <reg:2>                       -> AA 55 <status> | AA 55 <reg echo> <value:2>\n"
"  0x02  WRITE  <reg:2> <value:2>\n"
"  0x10  PROGRAM-DOWNLOAD  <fw:2> <auth:2> <trip_enable:1>\n"
"\n"
"[ register map ]\n"
"  reg 0    SIS_Trip_Function      1 = safety trip ARMED\n"
"  reg 1    Program_Download_Lock  write 1 to REQUEST a download window; the controller opens the lock\n"
"                                  briefly, then AUTO RE-LOCKS\n"
"  reg 2    Firmware_Compat_Flag   a download's declared firmware must match this or the checksum\n"
"                                  reconciliation rejects the new program\n"
"  reg 3    Last_Validated_Checksum\n"
"  reg 4    Checksum_Match         1 = running logic matches the validated checksum\n"
"  reg 5    Lock_Expiry            ms remaining on the open download lock (0 = closed)\n"
"  reg 100+ vendor/flag block      reads zero until the program change is validated and held\n"
"  (some diagnostic registers are reserved for commissioning use and are not listed here)\n";
}

// #06 GOOSE training monitor - mirrors ot::goose constants (UDP 6180; EXPECTED_APPID 0x0BB8;
// EXPECTED_CONFREV 1; the modelled dataset's single boolean = breaker position, true = open).
const char* gooseMonitorDoc() {
    return
"# Substation protection - GOOSE subscriber training monitor - config note\n"
"# UDP 6180 (training encapsulation; NOT L2 IEC 61850 GOOSE). Publisher broadcasts ~1 Hz.\n"
"\n"
"Subscribed APPID: 0x0BB8      Configuration revision (confRev): 1\n"
"\n"
"A subscriber accepts a frame on its APPID + confRev whose state number (stNum) is GREATER than the\n"
"last one it accepted, and treats it as a genuine state change. The sequence number (sqNum) is not\n"
"authenticated. Frames marked with the test flag are ignored.\n"
"\n"
"Dataset (modelled): a single boolean = breaker position.   false = closed,   true = OPEN.\n"
"\n"
"Observed normal stream: breaker CLOSED, stNum stable, sqNum incrementing. A real state change (e.g.\n"
"the breaker opening) publishes the new dataset with stNum incremented by one.\n";
}

// #08 SNMP field-device recovery - mirrors ot::snmp constants (UDP 161; read community public; write
// community disclosed in sysContact 1.3.6.1.2.1.1.4.0; enterprise MIB 1.3.6.1.4.1.42424 .1/.2/.3/.4/.5/.9).
const char* rtu07RecoveryDoc() {
    return
"# Field RTU-07 - SNMP firmware-recovery runbook (OT maintenance)\n"
"# Agent: UDP 161.  Read community: public.  Write community: published in sysContact (1.3.6.1.2.1.1.4.0).\n"
"\n"
"After an incident the RTU may be left running a tampered image. sysDescr shows \"[IMAGE CORRUPT]\" and\n"
"deviceHealth reads 0 when the running image is not the device's last-good image.\n"
"\n"
"[ enterprise recovery MIB  1.3.6.1.4.1.42424 ]\n"
"  .1.0  fwRecoveryMode    rw   0 = run, 1 = recovery\n"
"  .2.0  fwRunningImage    ro   image currently running\n"
"  .3.0  fwLastGoodImage   ro   the device's own known-good image\n"
"  .4.0  fwRestoreImage    wo   write the image to restore (reads back 0)\n"
"  .5.0  deviceHealth      ro   1 = healthy (running == last-good AND in run mode)\n"
"  .9.0  recoveryFlag      ro   populated once the device is verified recovered\n"
"\n"
"[ procedure ]  (all SETs require the write community)\n"
"  1. SET .1.0 = 1                        enter recovery mode\n"
"  2. SET .4.0 = <value read from .3.0>   restore the last-good image (the running image is read-only\n"
"                                         and cannot be written directly)\n"
"  3. SET .1.0 = 0                        return to run mode\n"
"Allow a few seconds in run mode for the health check to re-validate, then read .5.0 and .9.0.\n";
}

// #11 Unit-1 reactor - mirrors ot::reg (HR0/HR1/HR2 actuators; IR0/IR1/IR2/IR5/IR6; flag block IR32)
// and ot::cfg (safe bands TEMP 2900-3300, PRESSURE 600-1050, FLUX 300-650; SCRAM_TEMP 3800; 5 s hold).
// A sanitised SCADA commissioning worksheet: it maps the controls, sensors, safe-band mask and the
// over-temperature SCRAM + its recovery, so a player can stabilise the plant without reading source.
// It documents WHERE the sign-off token appears but never the token itself.
const char* reactorIoMapDoc() {
    return
"# Unit-1 Reactor - SCADA I/O map (commissioning worksheet, sanitized)\n"
"# Control network: Modbus/TCP, 10.0.1.1:502, unit id 1.  Holding via FC03/06/16, input via FC04.\n"
"\n"
"[ control - holding registers (writable; permille 0..1000 = 0..100.0%) ]\n"
"  HR0  CONTROL_ROD     rod position; higher = more neutron flux = more heat\n"
"  HR1  COOLING_WATER   primary pump setpoint; higher = more cooling (lowers core temperature)\n"
"  HR2  RELIEF_VALVE    relief-valve opening; higher = lower pressure\n"
"  HR3  TURBINE         turbine speed setpoint (NOT part of reactor stabilisation)\n"
"\n"
"[ sensors / status - input registers (read-only, FC04) ]\n"
"  IR0  NEUTRON_FLUX    permille of nominal\n"
"  IR1  CORE_TEMP       tenths of degC (e.g. 3100 = 310.0 C)\n"
"  IR2  PRESSURE        tenths of bar\n"
"  IR3  COOLANT_FLOW    measured pump flow (tracks HR1 with lag)\n"
"  IR4  TURBINE_RPM     measured turbine speed\n"
"  IR5  SAFE_BAND       bit0 = temp in band, bit1 = pressure in band, bit2 = flux in band (7 = all)\n"
"  IR6  PLANT_STATE     0 = running, 1 = SCRAM (emergency shutdown in progress)\n"
"\n"
"[ normal operating envelope (the safe band each IR5 bit reports) ]\n"
"  CORE_TEMP     2900 - 3300    (290.0 - 330.0 C)\n"
"  PRESSURE       600 - 1050    (60.0 - 105.0 bar)\n"
"  NEUTRON_FLUX   300 -  650    (permille)\n"
"  With all three inside their band, IR5 reads 7.\n"
"\n"
"[ over-temperature protection (SCRAM) ]\n"
"  If CORE_TEMP exceeds 3800 (380.0 C) the unit SCRAMs: the rods slam in (IR6 = 1) and the core\n"
"  cools. The trip CLEARS AUTOMATICALLY after a few seconds and the unit returns to its start-up\n"
"  state - re-stabilise from there. A SCRAM clears any stabilisation hold already in progress.\n"
"\n"
"[ commissioning sign-off ]\n"
"  Hold all three readings inside their band (IR5 = 7) continuously for 5 s. The commissioning data\n"
"  block at IR32..IR47 then reads back the sign-off token (two ASCII characters per register, most-\n"
"  significant byte first). It reads all zero until the 5 s hold completes, and re-locks if you\n"
"  leave the band.\n";
}

bool normalizeMac(const char* in, char out[18]) {
    if (!in) return false;
    int oct[6];
    int n = 0;
    const char* p = in;
    while (*p && n < 6) {
        while (*p == ':' || *p == '-' || *p == ' ') p++;
        if (!std::isxdigit((unsigned char)p[0]) || !std::isxdigit((unsigned char)p[1])) return false;
        auto hv = [](char c) {
            c = (char)std::tolower((unsigned char)c);
            return (c >= '0' && c <= '9') ? c - '0' : c - 'a' + 10;
        };
        oct[n++] = hv(p[0]) * 16 + hv(p[1]);
        p += 2;
    }
    while (*p == ':' || *p == '-' || *p == ' ') p++;
    if (n != 6 || *p != '\0') return false;
    std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                  oct[0], oct[1], oct[2], oct[3], oct[4], oct[5]);
    return true;
}

const char* vendorForMac(const char* mac) {
    char norm[18];
    if (!normalizeMac(mac, norm)) return nullptr;
    for (auto& o : OUIS) {
        // compare the first 8 chars "AA:BB:CC"
        if (std::strncmp(o.oui, norm, 8) == 0) return o.vendor;
    }
    return nullptr;
}

// ----- Historian Forensics (#07) ------------------------------------------------------------------
namespace forensics {

// #07 Historian SQL Injection: the solve is submitting the flag the player extracted from the
// simulated historian database (sim/HistSqlUnion) via UNION-based SQL injection. FORENSICS_FLAG is
// that single authoritative value (aliased from ot::sim::histsqlunion::FLAG). The old forensic path
// (recover REACTOR.SP's last-good value 480) is retired: that tag+value no longer completes #07.
// The historian access log (ACCESS[], still shown as evidence on /intranet) is background context now.
bool State::submit(const char* flag) {
    if (flag && std::strcmp(flag, FORENSICS_FLAG) == 0) solved_ = true;
    return solved_;
}

} // namespace forensics

// ----- Trojan Historian (#09) ---------------------------------------------------------------------
namespace trojan {

// Live trend with seq MISSING_SEQ (53) absent; backup is complete. PROVISIONAL sample values.
constexpr Sample LIVE[] = {
    { 50, 701, true }, { 51, 715, true }, { 52, 728, true },
    /* seq 53 deleted */
    { 54, 751, true }, { 55, 760, true }, { 56, 772, true },
};
constexpr int LIVE_COUNT = (int)(sizeof(LIVE) / sizeof(LIVE[0]));

constexpr Sample BACKUP[] = {
    { 50, 701, true }, { 51, 715, true }, { 52, 728, true },
    { 53, MISSING_VALUE, true },
    { 54, 751, true }, { 55, 760, true }, { 56, 772, true },
};
constexpr int BACKUP_COUNT = (int)(sizeof(BACKUP) / sizeof(BACKUP[0]));

int liveCount() { return LIVE_COUNT; }
const Sample& live(int i) { return LIVE[i]; }
int backupCount() { return BACKUP_COUNT; }
const Sample& backup(int i) { return BACKUP[i]; }

void State::reset(uint32_t) {
    restored_ = false;
    holding_ = false;
    hold_since_ms_ = 0;
    solved_ = false;
}

bool State::restore(uint32_t seq, int value, uint32_t now_ms) {
    bool correct = (seq == MISSING_SEQ && value == MISSING_VALUE);
    if (correct) {
        if (!holding_) { holding_ = true; hold_since_ms_ = now_ms; }
        restored_ = true;
    } else {
        // A wrong restore corrupts the record again and clears the stable timer.
        restored_ = false;
        holding_ = false;
    }
    tick(now_ms);
    return correct;
}

void State::tick(uint32_t now_ms) {
    if (holding_ && (now_ms - hold_since_ms_) >= HOLD_MS) solved_ = true;
}

uint32_t State::holdRemainingMs(uint32_t now_ms) const {
    if (solved_ || !holding_) return 0;
    const uint32_t elapsed = now_ms - hold_since_ms_;
    return elapsed >= HOLD_MS ? 0 : (HOLD_MS - elapsed);
}

} // namespace trojan

// ----- Device Takeover (#10) ----------------------------------------------------------------------
namespace takeover {

const char* impostorMac() { return IMPOSTOR_MAC; }
const char* impostorTs() { return IMPOSTOR_TS; }
const char* spoofedName() { return SPOOFED_NAME; }

bool State::submit(const char* mac) {
    char norm[18];
    if (!normalizeMac(mac, norm)) return false;
    if (std::strcmp(norm, IMPOSTOR_MAC) == 0) solved_ = true;
    return solved_;
}

} // namespace takeover

} // namespace ot::def
