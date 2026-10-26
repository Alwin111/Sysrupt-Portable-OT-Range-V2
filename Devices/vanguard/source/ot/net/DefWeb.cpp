// SPDX-License-Identifier: Apache-2.0
#include "DefWeb.h"

#include "OtHttp.h"
#include "../DefService.h"
#include "../GooseService.h"
#include "../HistorianService.h"
#include "../SnmpService.h"
#include "../sim/Defensive.h"
#include "../sim/HistSql.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Device-only rendering of the shared Defensive evidence + submission forms. All data + predicates
// come from sim/Defensive (host-tested) and DefService (latching state).
namespace ot::net {

namespace {

// Shared design tokens mirror the badge LCD palette (ui/Theme.h) so the defensive intranet reads as
// the same device as the on-screen UI and the offensive HMI: matte-black ground, near-white text,
// the Defensive blue accent, amber headers, green/red for result states, rounded panels. The viewport
// meta plus horizontally-scrollable tables keep the wide evidence tables usable on a phone.
// Same design system as OtHttp's STYLE, with the intranet "blue" accent (one cohesive product).
// Sans-serif prose + monospace for evidence tables / register values / telemetry; restrained palette;
// consistent controls, focus states, sticky compact nav with a current-page indicator.
constexpr auto* STYLE =
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>:root{--bg:#07090B;--pnl:#0E1113;--line:#223345;--blu:#2E9BFF;--grn:#3DFF8F;"
    "--txt:#EEF4F6;--mut:#9BA4A8;--amb:#FFC14D;--red:#E8593C;"
    "--mono:ui-monospace,'SFMono-Regular',Menlo,Consolas,monospace;"
    "--sans:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,system-ui,sans-serif}"
    "*{box-sizing:border-box}"
    "body{background:var(--bg);color:var(--txt);font-family:var(--sans);font-size:15px;line-height:1.55;"
    "margin:0 auto;padding:24px;max-width:1150px}"
    "h1{color:var(--blu);letter-spacing:1px;margin:0 0 8px;font-size:24px;font-weight:700}"
    ".sub{color:var(--mut);margin-bottom:16px;font-size:14px}"
    "code,pre,td,th,.box,input,.mono{font-family:var(--mono)}"
    "a{color:var(--blu)}a:hover{text-decoration:underline}"
    "table{border-collapse:collapse;margin:12px 0;width:100%;display:block;overflow-x:auto;font-size:14px}"
    "td,th{border:1px solid var(--line);padding:8px 12px;text-align:left;white-space:nowrap}"
    "th{color:var(--amb);background:#121820;position:sticky;top:0}"
    "tr:nth-child(even) td{background:rgba(255,255,255,.015)}"
    ".box{border:1px solid var(--line);background:var(--pnl);padding:16px;"
    "white-space:pre-wrap;margin:12px 0;border-radius:10px;font-size:14px;line-height:1.5}"
    ".ok{color:var(--grn)}.warn{color:var(--red)}"
    "input{background:var(--pnl);border:1px solid var(--line);color:var(--txt);font-size:15px;"
    "padding:9px 10px;margin:5px 0;max-width:100%;border-radius:6px}"
    "button{background:var(--blu);color:#00121f;border:0;padding:10px 20px;font-family:var(--sans);"
    "font-size:15px;font-weight:700;cursor:pointer;border-radius:6px}button:hover{filter:brightness(1.08)}"
    "a:focus-visible,button:focus-visible,input:focus-visible{outline:2px solid var(--amb);outline-offset:2px}"
    "nav{display:flex;flex-wrap:wrap;gap:4px 18px;margin-bottom:8px}"
    "nav a{font-size:15px;color:var(--mut);padding:2px 0}nav a:hover{color:var(--txt)}"
    "nav a.active{color:var(--blu);font-weight:700;border-bottom:2px solid var(--blu)}"
    "hr{border:0;border-top:1px solid var(--line);margin:4px 0 14px}"
    "@media(max-width:600px){body{padding:16px}h1{font-size:20px}}</style>";

// Corporate portal nav. The Historian is a SEPARATE internal system (its own property/port) and the
// standalone Evidence page is gone - every evidence file is now an attachment on its mail thread - so
// neither appears here (per the site-structure plan in the correction write-up).
constexpr auto* NAV =
    "<nav><a href=/intranet>HOME</a><a href=/mail>MAIL</a>"
    "<a href=/inventory>INVENTORY</a><a href=/investigate>SUBMIT</a>"
    "<a href=/hmi>REACTOR HMI</a></nav><hr>";

int hv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 0x20);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Extract + URL-decode an application/x-www-form-urlencoded field from a POST body.
bool formField(const char* body, const char* key, char* out, size_t cap) {
    char needle[24];
    std::snprintf(needle, sizeof(needle), "%s=", key);
    const char* p = std::strstr(body, needle);
    if (!p) { out[0] = '\0'; return false; }
    p += std::strlen(needle);
    size_t n = 0;
    while (*p && *p != '&' && n + 1 < cap) {
        char c = *p;
        if (c == '+') { c = ' '; p++; }
        else if (c == '%' && p[1] && p[2] && hv(p[1]) >= 0 && hv(p[2]) >= 0) { c = (char)(hv(p[1]) * 16 + hv(p[2])); p += 3; }
        else p++;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

// Strict non-negative-ish integer parse: trims surrounding whitespace, needs at least one digit and
// no trailing junk. Returns false for empty/garbage so the caller can report a precise format error
// instead of silently coercing "480xyz"/" 480 " to 480 (atoi would accept those). Addresses B1/B10.
bool parseIntStrict(const char* s, int& out) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return false;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    long v = 0; int digits = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); if (v > 2000000000L) v = 2000000000L; digits++; s++; }
    while (*s == ' ' || *s == '\t') s++;
    if (digits == 0 || *s != '\0') return false;
    out = (int)(neg ? -v : v);
    return true;
}

// Read a query-string value (returns false + empty string if absent). NOTE: httpd_query_key_value
// returns the value still percent-encoded; callers that need the decoded text (e.g. a SQL statement
// with spaces/quotes) must urlDecode() it. The existing numeric/timestamp callers do not.
bool queryVal(httpd_req_t* req, const char* key, char* out, size_t cap) {
    out[0] = '\0';
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1 || qlen > 256) return false;
    char q[256];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, cap) == ESP_OK;
}

// URL-decode a query-string value in place (%XX -> byte, '+' -> space). Same rule formField applies
// to POST bodies; query values arrive un-decoded from httpd_query_key_value.
void urlDecode(char* s) {
    char* o = s;
    for (char* p = s; *p;) {
        if (*p == '%' && p[1] && p[2] && hv(p[1]) >= 0 && hv(p[2]) >= 0) { *o++ = (char)(hv(p[1]) * 16 + hv(p[2])); p += 3; }
        else if (*p == '+') { *o++ = ' '; p++; }
        else { *o++ = *p++; }
    }
    *o = '\0';
}

// Every intranet route is gated behind the engineer login (the same session as the reactor HMI).
// Only /robots.txt and /control (which discloses the credentials) are reachable unauthenticated; any
// other route 302-redirects to the login page (/) until the request carries a valid session cookie.
bool gate(httpd_req_t* req) {
    if (engineerSessionValid(req)) return true;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);
    return false;
}

void head(httpd_req_t* req, const char* title) {
    setNoKeepAlive(req); // close the connection after each page (frees one of the 4 sockets) - U1
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><title>");
    httpd_resp_sendstr_chunk(req, title);
    httpd_resp_sendstr_chunk(req, "</title>");
    httpd_resp_sendstr_chunk(req, STYLE);
    httpd_resp_sendstr_chunk(req, "</head><body>");
    httpd_resp_sendstr_chunk(req, NAV);
}

esp_err_t finish(httpd_req_t* req) {
    // One-shot current-page highlight for the shared nav (no timers/polling; runs once, so it cannot
    // cause layout shift or leave work running after navigation).
    httpd_resp_sendstr_chunk(req,
        "<script>(function(){var p=location.pathname,n=document.querySelectorAll('nav a');"
        "for(var i=0;i<n.length;i++){if(n[i].getAttribute('href')===p)n[i].className='active';}})();</script>"
        "</body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

// region handlers

esp_err_t handleIntranet(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    head(req, "Sysrupt Intranet");
    httpd_resp_sendstr_chunk(req,
        "<h1>SYSRUPT OT INTRANET</h1><div class=sub>Plant status &amp; incident response workspace</div>"
        "<div class=box><b>OPEN INCIDENT 2026-03-14</b>\n"
        "Overnight anomalies across the plant network. See <a href=/mail>MAIL</a> for the tasking and its "
        "attached evidence, cross-check the asset <a href=/inventory>INVENTORY</a>, then record findings "
        "under <a href=/investigate>SUBMIT</a>. The process historian (HIST-04) is a separate internal "
        "system - its address is referenced in the relevant mail thread.\n\n"
        "Player tooling: download the unified <a href=\"/toolkit.zip\">SYSRUPT CLI toolkit</a> (sysrupt) "
        "for timing-sensitive and repetitive OT-protocol work.\n\n"
        "Scope: this is an isolated training range. No real industrial systems are reachable.</div>");
    // Live breaker position as believed by the bay GOOSE subscriber (Second Blackout, #06). The breaker
    // indicator lives here - a web surface the networked attacker can see - and reads the SUBSCRIBER's
    // accepted state (the same value a forged state change updates), so it is the correct place to
    // confirm whether an injected GOOSE frame actually took effect (not the reactor HMI, which is a
    // different process entirely).
    {
        auto& g = ot::GooseService::instance();
        const char* bs;
        if (!g.running())           bs = "<span class=warn>subscriber offline - open #06 on the badge</span>";
        else if (g.breakerOpen())   bs = "<span class=warn>OPEN - unauthorised state change ACCEPTED</span>";
        else                        bs = "<span class=ok>CLOSED - nominal</span>";
        char gb[520];
        std::snprintf(gb, sizeof(gb),
            "<div class=box><b>PROTECTION - BAY GOOSE SUBSCRIBER</b>\n"
            "Breaker position as currently believed by the bay LAN GOOSE subscriber (UDP 6180). This "
            "reflects the subscriber's ACCEPTED state - the same value a forged state change updates - so "
            "this is where to confirm whether an injected GOOSE frame actually took effect.\n\n"
            "Breaker: %s\nLast accepted state number (stNum): %u</div>",
            bs, (unsigned)g.lastStNum());
        httpd_resp_sendstr_chunk(req, gb);
    }
    return finish(req);
}

esp_err_t handleMail(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    char idStr[8];
    bool one = queryVal(req, "id", idStr, sizeof(idStr));
    int id = one ? atoi(idStr) : -1;
    head(req, "Webmail");
    httpd_resp_sendstr_chunk(req, "<h1>IR WEBMAIL</h1><div class=sub>inbox - ir-team@sysrupt.local</div>");
    if (id >= 0 && id < def::threadCount()) {
        // Thread view: each message in the conversation, with its attachment inline as a chip.
        const def::MailThread& t = def::thread(id);
        char hdr[200];
        std::snprintf(hdr, sizeof(hdr), "<div class=box style='border-color:var(--amb)'><b>%s</b>  "
                      "<span class=sub style='display:inline'>(%d message%s)</span></div>",
                      t.subject, t.msgCount, t.msgCount == 1 ? "" : "s");
        httpd_resp_sendstr_chunk(req, hdr);
        for (int m = 0; m < t.msgCount; m++) {
            const def::MailMsg& e = t.msgs[m];
            char buf[1300]; // headroom so a longer message body is not snprintf-truncated mid-sentence
            std::snprintf(buf, sizeof(buf),
                "<div class=box><b>From:</b> %s\n<b>Date:</b> %s\n\n%s</div>",
                e.from, e.date, e.body);
            httpd_resp_sendstr_chunk(req, buf);
            if (e.attachment) {
                char chip[220];
                std::snprintf(chip, sizeof(chip),
                    "<div style='margin:-6px 0 14px;font-family:var(--mono)'>&#128206; "
                    "<a href=\"/evidence?f=%s\">%s</a> "
                    "<span class=sub style='display:inline'>- attachment</span></div>",
                    e.attachment, e.attachment);
                httpd_resp_sendstr_chunk(req, chip);
            }
        }
        httpd_resp_sendstr_chunk(req, "<a href=/mail>&larr; inbox</a>");
    } else {
        // Inbox: one row per thread. Subject shows a reply count so threads with more context stand out;
        // the Date column shows the thread's latest activity.
        httpd_resp_sendstr_chunk(req, "<table><tr><th>#</th><th>From</th><th>Date</th><th>Subject</th></tr>");
        for (int i = 0; i < def::threadCount(); i++) {
            const def::MailThread& t = def::thread(i);
            const def::MailMsg& first = t.msgs[0];
            const def::MailMsg& last = t.msgs[t.msgCount - 1];
            char cnt[96]; cnt[0] = '\0';
            if (t.msgCount > 1)
                std::snprintf(cnt, sizeof(cnt), "  <span class=sub style='display:inline'>(%d messages)</span>", t.msgCount);
            char row[520];
            std::snprintf(row, sizeof(row),
                "<tr><td>%d</td><td>%s</td><td>%s</td><td><a href=\"/mail?id=%d\">%s</a>%s</td></tr>",
                i, first.from, last.date, i, t.subject, cnt);
            httpd_resp_sendstr_chunk(req, row);
        }
        httpd_resp_sendstr_chunk(req, "</table>");
    }
    return finish(req);
}

esp_err_t handleInventory(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    head(req, "Asset Inventory");
    httpd_resp_sendstr_chunk(req,
        "<h1>ENGINEERING ASSET INVENTORY</h1><div class=sub>trusted plant devices</div>"
        "<table><tr><th>Device</th><th>Role</th><th>IP</th><th>MAC</th><th>Vendor</th><th>Firmware</th></tr>");
    for (int i = 0; i < def::assetCount(); i++) {
        const def::Asset& a = def::asset(i);
        char row[320];
        std::snprintf(row, sizeof(row),
            "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>",
            a.name, a.role, a.ip, a.mac, a.vendor, a.firmware);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req,
        "</table><h1 style=font-size:15px>OUI REGISTRY</h1>"
        "<div class=sub>self-contained vendor lookup for the MAC prefixes in this range (provisional)</div>"
        "<table><tr><th>OUI</th><th>Vendor</th></tr>");
    for (int i = 0; i < def::ouiCount(); i++) {
        const def::OuiEntry& o = def::oui(i);
        char row[200];
        std::snprintf(row, sizeof(row), "<tr><td>%s</td><td>%s</td></tr>", o.oui, o.vendor);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
    return finish(req);
}

// Raw evidence fixtures as text/plain so they read like saved files.
esp_err_t handleEvidence(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    char f[32];
    bool have = queryVal(req, "f", f, sizeof(f));
    if (!have || f[0] == '\0') {
        // The standalone Evidence page has been removed: every evidence file is now an attachment on
        // the mail thread it belongs to. A bare hit on this endpoint goes to the inbox. The ?f=<name>
        // file-serving below is kept - it is what the mail attachment chips link to.
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/mail");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    setNoKeepAlive(req);
    httpd_resp_set_type(req, "text/plain");
    char line[256];
    if (std::strcmp(f, "dcp_capture") == 0) {
        httpd_resp_sendstr_chunk(req, "# PROFINET DCP capture (saved) - ts, src_mac, station_name, ip, kind\n");
        for (int i = 0; i < def::dcpCount(); i++) {
            const def::DcpFrame& d = def::dcp(i);
            std::snprintf(line, sizeof(line), "%s  %s  %-8s  %s  %s\n", d.ts, d.srcMac, d.deviceName, d.ip, d.kind);
            httpd_resp_sendstr_chunk(req, line);
        }
    } else if (std::strcmp(f, "asset_inventory") == 0) {
        for (int i = 0; i < def::assetCount(); i++) {
            const def::Asset& a = def::asset(i);
            std::snprintf(line, sizeof(line), "%-8s %-22s %s %s %s %s\n", a.name, a.role, a.ip, a.mac, a.vendor, a.firmware);
            httpd_resp_sendstr_chunk(req, line);
        }
    } else if (std::strcmp(f, "oui_registry") == 0) {
        for (int i = 0; i < def::ouiCount(); i++) {
            const def::OuiEntry& o = def::oui(i);
            std::snprintf(line, sizeof(line), "%s  %s\n", o.oui, o.vendor);
            httpd_resp_sendstr_chunk(req, line);
        }
    } else if (std::strcmp(f, "historian_access_log") == 0) {
        for (int i = 0; i < def::accessLogCount(); i++) {
            const def::AccessLog& a = def::accessLog(i);
            std::snprintf(line, sizeof(line), "%s %-8s %-13s %-7s %s %s\n", a.ts, a.user, a.srcip, a.action, a.tag, a.external ? "EXTERNAL" : "");
            httpd_resp_sendstr_chunk(req, line);
        }
    } else if (std::strcmp(f, "historian_backup") == 0) {
        // TURBINE.VIB live-vs-backup trend (the Trojan Historian evidence, #09). One live sample was
        // deleted; the nightly backup still holds it. Showing both columns side by side keeps the gap
        // discoverable here now that the historian page itself is the SQL console.
        httpd_resp_sendstr_chunk(req,
            "# TURBINE.VIB live-vs-backup trend - seq, live, backup\n"
            "# one live sample was deleted (shown as '(gap)'); the nightly backup still holds it.\n"
            "# restore the missing record (seq + value) under SUBMIT and keep it stable.\n");
        for (int i = 0; i < def::trojan::backupCount(); i++) {
            const def::trojan::Sample& b = def::trojan::backup(i);
            const char* liveVal = "(gap)";
            char lv[16];
            for (int j = 0; j < def::trojan::liveCount(); j++) {
                if (def::trojan::live(j).seq == b.seq) { std::snprintf(lv, sizeof(lv), "%d", def::trojan::live(j).value); liveVal = lv; break; }
            }
            std::snprintf(line, sizeof(line), "%u  %s  %d\n", (unsigned)b.seq, liveVal, b.value);
            httpd_resp_sendstr_chunk(req, line);
        }
    } else if (std::strcmp(f, "pump_capture") == 0) {
        // Saved raw capture of the pump-bus radio gateway (Challenge 4 companion asset). It documents
        // which feeder is the flow-control valve and shows the normal frame format + decoy feeders,
        // WITHOUT disclosing a usable command: every captured sequence number is already spent, so a
        // verbatim replay is rejected - the player must still reverse the frame/checksum rule and
        // craft a fresh, higher-sequence SELECT+OPEN against the live service on 10.0.1.1:7700.
        httpd_resp_sendstr_chunk(req,
            "# pump-bus capture (legacy radio gateway, TCP 7700) - raw frames; C=client S=server\n"
            "# context: feeder-07 is the main flow-control valve; feeders 02/05/09/0C are decoys.\n"
            "# anti-replay: a fresh session starts ABOVE every sequence number in this capture, so a\n"
            "#   verbatim replay is always rejected - craft a fresh SELECT+OPEN with a higher, unused seq.\n"
            "# status block: the gateway's status/flag block is read back on the SAME socket with action\n"
            "#   byte 05 (target = word index), reply 50 55 00 <hi> <lo>; it reads zero until the target\n"
            "#   valve has been SELECTed, OPENed and held open.\n");
        httpd_resp_sendstr_chunk(req,
            "C  50 55 01 DC 02 01 00 85\n" "S  50 55 00\n"
            "C  50 55 01 DD 02 02 00 87\n" "S  50 55 00\n"
            "C  50 55 01 DE 02 03 00 89\n" "S  50 55 00\n"
            "C  50 55 01 DF 05 01 00 8B\n" "S  50 55 00\n"
            "C  50 55 01 E0 05 02 00 8D\n" "S  50 55 00\n"
            "C  50 55 01 E1 05 03 00 8F\n" "S  50 55 00\n"
            "C  50 55 01 E2 07 01 00 90\n" "S  50 55 00\n"
            "C  50 55 01 E3 07 02 00 92\n" "S  50 55 00\n");
        httpd_resp_sendstr_chunk(req,
            "C  50 55 01 E4 07 03 00 94\n" "S  50 55 00\n"
            "C  50 55 01 E5 09 01 00 95\n" "S  50 55 00\n"
            "C  50 55 01 E6 09 02 00 97\n" "S  50 55 00\n"
            "C  50 55 01 E7 09 03 00 99\n" "S  50 55 00\n"
            "C  50 55 01 E8 0C 01 00 9B\n" "S  50 55 00\n"
            "C  50 55 01 E9 0C 02 00 9D\n" "S  50 55 00\n"
            "C  50 55 01 EA 07 01 00 98\n" "S  50 55 00\n"
            "C  50 55 01 EB 07 02 00 9A\n" "S  50 55 00\n");
    } else if (std::strcmp(f, "rtu560_points") == 0) {
        httpd_resp_sendstr_chunk(req, def::rtu560PointsDoc());
    } else if (std::strcmp(f, "sis_maint_note") == 0) {
        httpd_resp_sendstr_chunk(req, def::sisMaintDoc());
    } else if (std::strcmp(f, "goose_monitor") == 0) {
        httpd_resp_sendstr_chunk(req, def::gooseMonitorDoc());
    } else if (std::strcmp(f, "rtu07_recovery") == 0) {
        httpd_resp_sendstr_chunk(req, def::rtu07RecoveryDoc());
    } else if (std::strcmp(f, "reactor_io_map") == 0) {
        httpd_resp_sendstr_chunk(req, def::reactorIoMapDoc());
    } else {
        httpd_resp_sendstr_chunk(req, "unknown evidence file\n");
    }
    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t handleInvestigate(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    head(req, "Submit Findings");
    httpd_resp_sendstr_chunk(req,
        "<h1>RECORD FINDINGS</h1><div class=sub>a flag is issued only on a correct finding</div>"
        "<div class=box><b>#07 HISTORIAN SQL INJECTION</b>\nSubmit the flag you extracted from the historian database via SQL injection (console at 10.0.1.1/hist).\n"
        "<form method=POST action=/submit><input type=hidden name=ch value=forensics>"
        "flag <input name=flag size=34 autocomplete=off spellcheck=false> <button>SUBMIT</button></form></div>"
        "<div class=box><b>#09 TROJAN HISTORIAN</b>\nRestore the deleted TURBINE.VIB record (seq + value).\n"
        "<form method=POST action=/submit><input type=hidden name=ch value=trojan>"
        "seq <input name=seq size=6> value <input name=value size=8> <button>RESTORE</button></form></div>"
        "<div class=box><b>#10 DEVICE TAKEOVER</b>\nThe impostor station's MAC from the DCP capture.\n"
        "<form method=POST action=/submit><input type=hidden name=ch value=takeover>"
        "mac <input name=mac size=20> <button>SUBMIT</button></form></div>");
    // B4: #08 Field Device Recovery is a real defensive challenge, but it completes over SNMP (not a
    // web form). Surface it here as a read-only status so a player hunting the submission page can find
    // it, with a pointer to the runbook and the device's live health. No answer/flag is disclosed.
    {
        auto& s = ot::SnmpService::instance();
        const char* st;
        if (!s.running())      st = "<span class=warn>agent not started - open #08 on the badge to bring the RTU online</span>";
        else if (s.solved())   st = "<span class=ok>RECOVERED - device verified healthy</span>";
        else if (s.healthy())  st = "<span class=ok>healthy - verifying recovery</span>";
        else                   st = "<span class=warn>UNHEALTHY - running image is not the last-good image</span>";
        char b[400];
        std::snprintf(b, sizeof(b),
            "<div class=box><b>#08 FIELD DEVICE RECOVERY</b>\nThis one is completed over <b>SNMP (UDP 161)</b>, "
            "not a web form: restore RTU-07 to its last-good firmware using the runbook "
            "(<a href=\"/evidence?f=rtu07_recovery\">rtu07_recovery</a>). Once it reads healthy, the badge "
            "issues the flag.\nLive device status: %s.</div>", st);
        httpd_resp_sendstr_chunk(req, b);
    }
    return finish(req);
}

esp_err_t handleSubmit(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    char body[256] = {};
    int total = 0, r;
    while ((r = httpd_req_recv(req, body + total, sizeof(body) - 1 - total)) > 0) {
        total += r;
        if (total >= (int)sizeof(body) - 1) break;
    }
    if (total < 0) total = 0;
    body[total] = '\0';   // never index with a negative recv result (malformed request safety)

    char ch[16] = {};
    formField(body, "ch", ch, sizeof(ch));

    // B1: distinguish the real failure cause. Default is a genuine wrong-answer; the branches override
    // with a precise "missing field" / "bad format" message when that is what actually happened. No
    // branch discloses the expected value or a hidden validation rule.
    const char* WRONG = "Incorrect - that doesn't match the evidence. Re-check your finding and try again.";
    bool ok = false;        // correct AND fully solved (flag issued)
    bool pending = false;   // correct but still completing (trojan stability hold) - B2
    const char* flag = nullptr;
    const char* msg = WRONG;
    char msgbuf[160];
    DefService& svc = DefService::instance();

    if (std::strcmp(ch, "forensics") == 0) {
        // #07 Historian SQL Injection: the finding is the flag extracted from the historian database
        // via SQL injection. Validate the submitted flag against the authoritative value; the expected
        // flag is never disclosed here before a correct submission.
        char fl[48] = {};
        bool hasFlag = formField(body, "flag", fl, sizeof(fl)) && fl[0];
        if (!hasFlag) {
            msg = "Missing input - paste the flag you extracted from the historian database.";
        } else if (svc.lock()) {
            ok = svc.forensics().submit(fl);
            if (ok) flag = svc.forensics().flag();
            svc.unlock();
        }
    } else if (std::strcmp(ch, "trojan") == 0) {
        char seq[16] = {}, val[16] = {};
        bool hasSeq = formField(body, "seq", seq, sizeof(seq)) && seq[0];
        bool hasVal = formField(body, "value", val, sizeof(val)) && val[0];
        int seqN = 0, valN = 0;
        if (!hasSeq || !hasVal) {
            msg = "Missing input - enter both the sequence number and the value to restore.";
        } else if (!parseIntStrict(seq, seqN) || seqN < 0 || !parseIntStrict(val, valN)) {
            msg = "seq and value must be whole numbers from the backup trend.";
        } else if (svc.lock()) {
            const uint32_t now = svc.nowMs();
            bool correct = svc.trojan().restore((uint32_t)seqN, valN, now);
            ok = svc.trojan().solved();
            if (ok) {
                flag = svc.trojan().flag();
            } else if (correct) {
                // B2: correct restore, hold still running - say so clearly with the remaining time,
                // instead of a vague message that reads like failure.
                pending = true;
                uint32_t rem = svc.trojan().holdRemainingMs(now);
                std::snprintf(msgbuf, sizeof(msgbuf),
                    "Correct - record restored. Keep it stable ~%u s more; the badge then marks #09 "
                    "solved. Re-submit the same values after the hold to see your flag here.",
                    (unsigned)((rem + 999) / 1000));
                msg = msgbuf;
            }
            svc.unlock();
        }
    } else if (std::strcmp(ch, "takeover") == 0) {
        char mac[24] = {};
        bool hasMac = formField(body, "mac", mac, sizeof(mac)) && mac[0];
        char norm[18];
        if (!hasMac) {
            msg = "Missing input - enter the impostor station's MAC address.";
        } else if (!def::normalizeMac(mac, norm)) {
            msg = "That isn't a valid MAC address - use 6 hex octets, e.g. AA:BB:CC:DD:EE:FF.";
        } else if (svc.lock()) {
            ok = svc.takeover().submit(mac);
            if (ok) flag = svc.takeover().flag();
            svc.unlock();
        }
    } else {
        msg = "Unknown submission - use the forms on the SUBMIT page.";
    }

    head(req, "Result");
    httpd_resp_sendstr_chunk(req, "<h1>SUBMISSION RESULT</h1>");
    char buf[260];
    if (ok && flag) {
        std::snprintf(buf, sizeof(buf), "<div class=box><span class=ok>CORRECT</span>\nFlag: <b>%s</b></div>", flag);
    } else if (pending) {
        std::snprintf(buf, sizeof(buf), "<div class=box><span class=ok>RESTORED - CORRECT</span>\n%s</div>", msg);
    } else {
        std::snprintf(buf, sizeof(buf), "<div class=box><span class=warn>%s</span></div>", msg);
    }
    httpd_resp_sendstr_chunk(req, buf);
    httpd_resp_sendstr_chunk(req, "<a href=/investigate>&larr; back</a>");
    return finish(req);
}

httpd_uri_t mk(const char* path, httpd_method_t m, esp_err_t (*fn)(httpd_req_t*)) {
    httpd_uri_t u = {};
    u.uri = path; u.method = m; u.handler = fn;
    return u;
}

// endregion

} // namespace

void registerDefRoutes(httpd_handle_t server) {
    const httpd_uri_t uris[] = {
        mk("/intranet", HTTP_GET, handleIntranet),
        mk("/mail", HTTP_GET, handleMail),
        mk("/inventory", HTTP_GET, handleInventory),
        mk("/evidence", HTTP_GET, handleEvidence),
        mk("/investigate", HTTP_GET, handleInvestigate),
        mk("/submit", HTTP_POST, handleSubmit),
    };
    for (const auto& u : uris) httpd_register_uri_handler(server, &u);
}

} // namespace ot::net
