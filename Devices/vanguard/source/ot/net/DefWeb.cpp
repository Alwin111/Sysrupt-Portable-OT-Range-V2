// SPDX-License-Identifier: Apache-2.0
#include "DefWeb.h"

#include "OtHttp.h"
#include "../DefService.h"
#include "../GooseService.h"
#include "../SnmpService.h"
#include "../sim/Defensive.h"

#include <cctype>
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
// One cohesive look across the whole web HMI: the defensive intranet uses the shared SYSRUPT design
// system (ot::net::kWebStyle, defined in OtHttp.h) rather than its own palette.
constexpr auto* STYLE = ot::net::kWebStyle;

// Corporate portal nav. The Historian is a SEPARATE internal system (its own property/port) and the
// standalone Evidence page is gone - every evidence file is now an attachment on its mail thread - so
// neither appears here (per the site-structure plan in the correction write-up).
// Shared full-width deep-brown header for every defensive page (one cohesive nuclear-plant identity,
// matching the Reactor HMI). Emitted right after <body> so its negative top margin sits flush to the top.
constexpr auto* NAV =
    "<header class=topbar><div class=topbar-inner>"
    "<span class=brand>NUCLEAR POWER OPERATIONS<i>Unit 1 | Plant Operations &amp; Incident Investigation</i></span>"
    "<nav><a href=/intranet>Home</a><a href=/mail>Mail</a><a href=/inventory>Inventory</a>"
    "<a href=/investigate>Submit</a><a href=/hmi>Reactor</a></nav>"
    "</div></header>";

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

// --- Webmail presentation helpers (cosmetic only; no challenge data) ---

// Up to two uppercase initials from a sender's local part for the avatar tile (e.g. "k.mensah" -> "KM").
void initials(const char* from, char* out) {
    int n = 0;
    for (const char* p = from; *p && *p != '@' && n < 2; p++)
        if (std::isalpha((unsigned char)*p)) out[n++] = (char)std::toupper((unsigned char)*p);
    if (n == 0) out[n++] = '?';
    out[n] = '\0';
}

// Stable avatar colour for a sender (same address -> same tile colour), from the shared palette.
const char* avatarColor(const char* from) {
    static const char* kPal[6] = {"#703f37", "#7d94b5", "#5f6e45", "#4c6485", "#8a5a4e", "#6b7f9e"};
    unsigned h = 0;
    for (const char* p = from; *p; p++) h = h * 31 + (unsigned char)*p;
    return kPal[h % 6];
}

// Collapse a message body to a single-line inbox preview. Returns true if the body was longer than
// the preview (so the caller can append an ellipsis). Newlines/tabs -> spaces; runs of spaces folded.
bool snippet(const char* body, char* out, size_t maxchars) {
    size_t n = 0; bool sp = false; const char* p = body;
    while (*p && n < maxchars) {
        char c = *p++;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') { if (n == 0 || sp) continue; sp = true; } else sp = false;
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return *p != '\0';
}

// Download filename + type-chip label/class for an evidence attachment, so it reads (and saves) like a
// real file. Content is plain text, so everything is .txt except the access log (.log).
void attachmentMeta(const char* key, char* fname, size_t cap, const char*& chip, const char*& cls, const char*& type) {
    if (std::strcmp(key, "historian_access_log") == 0) {
        std::snprintf(fname, cap, "historian_access.log");
        chip = "LOG"; cls = "log"; type = "Access log";
    } else {
        std::snprintf(fname, cap, "%s.txt", key);
        chip = "TXT"; cls = ""; type = "Text document";
    }
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
        "<footer>SYSRUPT OT Range &middot; Nuclear Power Operations &mdash; Unit 1 &middot; "
        "Internal use only &middot; isolated training environment</footer>"
        "<script>(function(){var p=location.pathname,n=document.querySelectorAll('nav a');"
        "for(var i=0;i<n.length;i++){if(n[i].getAttribute('href')===p)n[i].className='active';}})();</script>"
        "</body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

// region handlers

esp_err_t handleIntranet(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    head(req, "Sysrupt Intranet");
    {
        char ph[760];
        std::snprintf(ph, sizeof(ph),
            "<div class=phead><div><h1>Operations Overview</h1>"
            "<div class=sub>Unit 1 &middot; plant status &amp; incident response workspace</div></div>"
            "<div class=meta>NUCLEAR POWER OPERATIONS<br>Shift: NIGHT &middot; 2026-03-14<br>Classification: INTERNAL</div></div>"
            "<div class=stats>"
            "<div class=stat-card><b>1</b><span>Open incident</span></div>"
            "<div class=stat-card><b>%d</b><span>Monitored assets</span></div>"
            "<div class=stat-card><b>%d</b><span>Mail threads</span></div>"
            "<div class=stat-card><b>HIST-04</b><span>Process historian</span></div>"
            "</div>", def::assetCount(), def::threadCount());
        httpd_resp_sendstr_chunk(req, ph);
    }
    httpd_resp_sendstr_chunk(req,
        "<div class=\"icard alert\"><div class=ico>&#9888;</div><div class=bd>"
        "<div class=ttl>OPEN INCIDENT &middot; 2026-03-14</div>"
        "<p>Overnight anomalies across the plant network. See <a href=/mail>Mail</a> for the tasking and its "
        "attached evidence, cross-check the asset <a href=/inventory>Inventory</a>, then record findings "
        "under <a href=/investigate>Submit</a>. The process historian (HIST-04) is a separate internal "
        "system &mdash; the relevant mail thread explains how to reach it.</p>"
        "<p>Player tooling: download the unified <a href=\"/toolkit.zip\">SYSRUPT CLI toolkit</a> (sysrupt) "
        "for timing-sensitive and repetitive OT-protocol work.</p>"
        "<p class=hint>Scope: this is an isolated training range. No real industrial systems are reachable.</p>"
        "</div></div>");
    // Live breaker position as believed by the bay GOOSE subscriber (Second Blackout, #06). The breaker
    // indicator lives here - a web surface the networked attacker can see - and reads the SUBSCRIBER's
    // accepted state (the same value a forged state change updates), so it is the correct place to
    // confirm whether an injected GOOSE frame actually took effect (not the reactor HMI, which is a
    // different process entirely).
    {
        auto& g = ot::GooseService::instance();
        const char* cls;   // icard accent
        const char* pill;  // breaker status pill
        if (!g.running()) {
            cls = "alert"; pill = "<span class=\"pill warn\">SUBSCRIBER OFFLINE</span> "
                                  "<span class=hint style='display:inline'>open #06 on the badge</span>";
        } else if (g.breakerOpen()) {
            cls = "bad";   pill = "<span class=\"pill alarm\">OPEN &middot; unauthorised state change ACCEPTED</span>";
        } else {
            cls = "good";  pill = "<span class=\"pill ok\">CLOSED &middot; nominal</span>";
        }
        char gb[860];
        std::snprintf(gb, sizeof(gb),
            "<div class=\"icard %s\"><div class=ico>&#9889;</div><div class=bd>"
            "<div class=ttl>PROTECTION &middot; BAY GOOSE SUBSCRIBER</div>"
            "<p>Breaker position as currently believed by the bay LAN GOOSE subscriber (UDP 6180). This "
            "reflects the subscriber's ACCEPTED state &mdash; the same value a forged state change updates "
            "&mdash; so this is where to confirm whether an injected GOOSE frame actually took effect.</p>"
            "<p>Breaker: %s</p>"
            "<p class=hint>Last accepted state number (stNum): %u</p></div></div>",
            cls, pill, (unsigned)g.lastStNum());
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
    if (id >= 0 && id < def::threadCount()) {
        // Thread view: a message card per reply, each with an avatar header and its evidence file as a
        // real downloadable attachment card.
        const def::MailThread& t = def::thread(id);
        char top[360];
        std::snprintf(top, sizeof(top),
            "<div class=thtop>"
            "<a href=/mail class=btn-sec style='text-decoration:none;padding:7px 14px;margin:0'>&larr; Inbox</a>"
            "<h2>%s</h2><span class=thn>%d message%s</span></div>",
            t.subject, t.msgCount, t.msgCount == 1 ? "" : "s");
        httpd_resp_sendstr_chunk(req, top);
        for (int m = 0; m < t.msgCount; m++) {
            const def::MailMsg& e = t.msgs[m];
            char ini[4]; initials(e.from, ini);
            char buf[1800]; // headroom so a longer message body is not snprintf-truncated mid-sentence
            std::snprintf(buf, sizeof(buf),
                "<div class=msg><div class=mhd>"
                "<span class=avatar style=\"--c:%s\">%s</span>"
                "<span class=mwho><span class=mname>%s</span><span class=mto>to ir-team@sysrupt.local</span></span>"
                "<span class=mdate>%s</span></div><p class=mbody>%s</p>",
                avatarColor(e.from), ini, e.from, e.date, e.body);
            httpd_resp_sendstr_chunk(req, buf);
            if (e.attachment) {
                char fn[48]; const char* chip; const char* cls; const char* type;
                attachmentMeta(e.attachment, fn, sizeof(fn), chip, cls, type);
                char fc[480];
                std::snprintf(fc, sizeof(fc),
                    "<div class=files><a class=file href=\"/evidence?f=%s&amp;dl=1\" download=\"%s\">"
                    "<span class=\"fx %s\">%s</span>"
                    "<span class=fmeta><span class=fname>%s</span><span class=fsub>%s &middot; Download</span></span>"
                    "<span class=fdl>&#8681;</span></a></div>",
                    e.attachment, fn, cls, chip, fn, type);
                httpd_resp_sendstr_chunk(req, fc);
            }
            httpd_resp_sendstr_chunk(req, "</div>"); // close .msg
        }
    } else {
        // Inbox: one avatar row per thread, latest activity on the right, a paperclip when the thread
        // carries an attachment and a count badge for multi-message threads.
        char mh[260];
        std::snprintf(mh, sizeof(mh),
            "<h1>IR Webmail</h1><div class=sub>ir-team@sysrupt.local</div>"
            "<div class=mailbox><div class=mbhead><span class=mbtitle>Inbox</span>"
            "<span class=mbcount>%d conversations</span></div><ul class=mlist>", def::threadCount());
        httpd_resp_sendstr_chunk(req, mh);
        for (int i = 0; i < def::threadCount(); i++) {
            const def::MailThread& t = def::thread(i);
            const def::MailMsg& first = t.msgs[0];
            const def::MailMsg& last = t.msgs[t.msgCount - 1];
            char ini[4]; initials(first.from, ini);
            char sn[96]; bool more = snippet(first.body, sn, 76);
            bool clip = false;
            for (int m = 0; m < t.msgCount; m++) if (t.msgs[m].attachment) { clip = true; break; }
            char badge[48]; badge[0] = '\0';
            if (t.msgCount > 1) std::snprintf(badge, sizeof(badge), "<span class=mbadge>%d</span>", t.msgCount);
            char row[1200];
            std::snprintf(row, sizeof(row),
                "<li class=mrow><a class=mlink href=\"/mail?id=%d\">"
                "<span class=mdot></span><span class=avatar style=\"--c:%s\">%s</span>"
                "<span class=mmid><span class=mtop><span class=mfrom>%s</span><span class=mdate>%s</span></span>"
                "<span class=mbot><span class=msub>%s</span> <span class=msnip>&mdash; %s%s</span></span></span>"
                "%s%s</a></li>",
                i, avatarColor(first.from), ini, first.from, last.date, t.subject,
                sn, more ? "&hellip;" : "",
                clip ? "<span class=mclip>&#128206;</span>" : "", badge);
            httpd_resp_sendstr_chunk(req, row);
        }
        httpd_resp_sendstr_chunk(req, "</ul></div>");
    }
    return finish(req);
}

esp_err_t handleInventory(httpd_req_t* req) {
    if (!gate(req)) return ESP_OK;
    head(req, "Asset Inventory");
    {
        char ph[440];
        std::snprintf(ph, sizeof(ph),
            "<div class=phead><div><h1>Engineering Asset Inventory</h1>"
            "<div class=sub>Trusted plant devices on the Unit-1 control network</div></div>"
            "<div class=meta>Network: 10.0.1.0/24<br>%d devices registered</div></div>"
            "<div class=shead><h2>Plant devices</h2></div>"
            "<div class=table-wrap><table><tr><th>Device</th><th>Role</th><th>IP</th><th>MAC</th>"
            "<th>Vendor</th><th>Firmware</th></tr>", def::assetCount());
        httpd_resp_sendstr_chunk(req, ph);
    }
    for (int i = 0; i < def::assetCount(); i++) {
        const def::Asset& a = def::asset(i);
        char row[320];
        std::snprintf(row, sizeof(row),
            "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>",
            a.name, a.role, a.ip, a.mac, a.vendor, a.firmware);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req,
        "</table></div>"
        "<div class=shead><h2>OUI registry</h2>"
        "<span class=c>vendor lookup for the MAC prefixes in this range (provisional)</span></div>"
        "<div class=table-wrap><table><tr><th>OUI</th><th>Vendor</th></tr>");
    for (int i = 0; i < def::ouiCount(); i++) {
        const def::OuiEntry& o = def::oui(i);
        char row[200];
        std::snprintf(row, sizeof(row), "<tr><td>%s</td><td>%s</td></tr>", o.oui, o.vendor);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</table></div>");
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
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    // When reached via a mail attachment's Download button (?dl=1), send it as a real file download with
    // a proper filename instead of rendering inline. cdisp must outlive the response headers (sent on the
    // first chunk below), so it is declared in this function scope.
    char cdisp[80];
    {
        char dl[4];
        if (queryVal(req, "dl", dl, sizeof(dl))) {
            char fn[48]; const char* chip; const char* cls; const char* type;
            attachmentMeta(f, fn, sizeof(fn), chip, cls, type);
            std::snprintf(cdisp, sizeof(cdisp), "attachment; filename=\"%s\"", fn);
            httpd_resp_set_hdr(req, "Content-Disposition", cdisp);
        }
    }
    char line[256];
    if (std::strcmp(f, "dcp_capture") == 0) {
        // Rendered as a Wireshark/tshark "Export Packet Dissections as Plain Text" file: a packet-list
        // summary first, then expanded dissections for the PN-DCP and ARP frames. MACs are lower-cased
        // as Wireshark shows them (the data model keeps the canonical upper-case form). The answer is
        // not called out - the player has to spot the duplicated NameOfStation across two source MACs.
        auto lc = [](const char* s, char* out, size_t cap) {
            size_t i = 0;
            for (; s[i] && i + 1 < cap; i++) { char c = s[i]; out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
            out[i] = '\0';
        };
        httpd_resp_sendstr_chunk(req,
            "# Wireshark - Export Packet Dissections as Plain Text\n"
            "# File: profinet_dcp_overnight.pcapng   Interface: cell network 10.0.1.0/24\n"
            "# Display filter: pn_dcp || arp || lldp\n"
            "\n"
            "No.  Time      Source             Destination        Proto   Len  Info\n");
        char sm[18], dm[18];
        for (int i = 0; i < def::dcpCount(); i++) {
            const def::DcpFrame& d = def::dcp(i);
            lc(d.srcMac, sm, sizeof(sm)); lc(d.dstMac, dm, sizeof(dm));
            int len = (std::strcmp(d.proto, "ARP") == 0) ? 60 : (std::strcmp(d.proto, "LLDP") == 0) ? 118 : 68;
            std::snprintf(line, sizeof(line), "%3d  %.8s  %-17s  %-17s  %-6s  %3d  %s\n",
                          i + 1, d.ts + 11, sm, dm, d.proto, len, d.info);
            httpd_resp_sendstr_chunk(req, line);
        }
        httpd_resp_sendstr_chunk(req, "\n===== Frame details (PROFINET DCP and ARP) =====\n");
        for (int i = 0; i < def::dcpCount(); i++) {
            const def::DcpFrame& d = def::dcp(i);
            if (std::strcmp(d.proto, "LLDP") == 0) continue;   // link-layer noise - summary only
            lc(d.srcMac, sm, sizeof(sm)); lc(d.dstMac, dm, sizeof(dm));
            char det[512];
            if (std::strcmp(d.proto, "ARP") == 0) {
                std::snprintf(det, sizeof(det),
                    "\nFrame %d: 60 bytes on wire\n"
                    "Ethernet II, Src: %s, Dst: %s\n"
                    "Address Resolution Protocol (%s)\n"
                    "    Sender MAC: %s   Sender IP: %s\n"
                    "    Target  IP: 10.0.1.21\n",
                    i + 1, sm, dm,
                    std::strstr(d.info, "Gratuitous") ? "gratuitous" : "request",
                    sm, d.ip[0] ? d.ip : "-");
            } else {
                const char* svc = std::strstr(d.info, "Ident Req") ? "Identify (5) / Request" :
                                  std::strstr(d.info, "Ident Ok")  ? "Identify (5) / Response Success" :
                                                                     "Hello (6) / Request";
                char blocks[256] = {};
                if (d.deviceName[0]) {
                    char nm[32]; lc(d.deviceName, nm, sizeof(nm));
                    std::snprintf(blocks, sizeof(blocks),
                        "    Block: Device/NameOfStation   \"%s\"\n"
                        "    Block: IP/IPParameter   IP: %s, Subnet: 255.255.255.0, Gateway: 10.0.1.1\n",
                        nm, d.ip);
                }
                std::snprintf(det, sizeof(det),
                    "\nFrame %d: 68 bytes on wire\n"
                    "Ethernet II, Src: %s, Dst: %s\n"
                    "PROFINET acyclic Real-Time, DCP\n"
                    "PROFINET DCP, ServiceID: %s\n"
                    "%s",
                    i + 1, sm, dm, svc, blocks);
            }
            httpd_resp_sendstr_chunk(req, det);
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
        // #09 Trojan Historian evidence: TWO SEPARATE historian exports (no pre-joined diff, no gap
        // marked). Export 1 is the single-tag LIVE trend; export 2 is the multi-tag NIGHTLY BACKUP,
        // ordered by seq. The investigator filters the backup to the live trend's tag (TURBINE.VIB),
        // diffs the sequence numbers, and finds the one seq present in the backup but missing from the
        // live trend (the deleted record). Some live gaps are comms outages the backup lacks too
        // (decoys), so the first gap is not necessarily the answer.
        httpd_resp_sendstr_chunk(req,
            "# historian export 1/2 - LIVE TREND (HIST-04)\n"
            "# columns: tag,seq,value,quality\n");
        for (int i = 0; i < def::trojan::liveCount(); i++) {
            const def::trojan::Sample& s = def::trojan::live(i);
            std::snprintf(line, sizeof(line), "%s,%u,%d,Good\n", s.tag, (unsigned)s.seq, s.value);
            httpd_resp_sendstr_chunk(req, line);
        }
        httpd_resp_sendstr_chunk(req,
            "\n# historian export 2/2 - NIGHTLY BACKUP (HIST-04, all monitored tags)\n"
            "# columns: tag,seq,value,quality\n");
        for (int i = 0; i < def::trojan::backupCount(); i++) {
            const def::trojan::Sample& s = def::trojan::backup(i);
            std::snprintf(line, sizeof(line), "%s,%u,%d,Good\n", s.tag, (unsigned)s.seq, s.value);
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
        "<div class=phead><div><h1>Record Findings</h1>"
        "<div class=sub>a flag is issued only on a correct finding</div></div>"
        "<div class=meta>Incident 2026-03-14<br>ir-team@sysrupt.local</div></div>"
        "<div class=qcard><div class=qh><span class=qnum>#07</span><span class=qt>Historian SQL Injection</span></div>"
        "<p>Submit the flag you extracted from the historian database via SQL injection (the historian is a "
        "separate internal system &mdash; locate its console first).</p>"
        "<form class=qform method=POST action=/submit><input type=hidden name=ch value=forensics>"
        "<span class=flbl>flag</span><input name=flag size=34 autocomplete=off spellcheck=false placeholder=\"flag{...}\">"
        "<button>Submit</button></form></div>"
        "<div class=qcard><div class=qh><span class=qnum>#09</span><span class=qt>Trojan Historian</span></div>"
        "<p>Restore the deleted TURBINE.VIB record (seq + value).</p>"
        "<form class=qform method=POST action=/submit><input type=hidden name=ch value=trojan>"
        "<span class=flbl>seq</span><input name=seq size=6>"
        "<span class=flbl>value</span><input name=value size=8><button>Restore</button></form></div>"
        "<div class=qcard><div class=qh><span class=qnum>#10</span><span class=qt>Device Takeover</span></div>"
        "<p>The impostor station's MAC from the DCP capture.</p>"
        "<form class=qform method=POST action=/submit><input type=hidden name=ch value=takeover>"
        "<span class=flbl>mac</span><input name=mac size=20><button>Submit</button></form></div>");
    // B4: #08 Field Device Recovery is a real defensive challenge, but it completes over SNMP (not a
    // web form). Surface it here as a read-only status so a player hunting the submission page can find
    // it, with a pointer to the runbook and the device's live health. No answer/flag is disclosed.
    {
        auto& s = ot::SnmpService::instance();
        const char* st;
        if (!s.running())      st = "<span class=\"pill warn\">AGENT NOT STARTED</span> <span class=hint style='display:inline'>open #08 on the badge to bring the RTU online</span>";
        else if (s.solved())   st = "<span class=\"pill ok\">RECOVERED &middot; device verified healthy</span>";
        else if (s.healthy())  st = "<span class=\"pill ok\">HEALTHY &middot; verifying recovery</span>";
        else                   st = "<span class=\"pill warn\">UNHEALTHY &middot; running image is not the last-good image</span>";
        char b[700];
        std::snprintf(b, sizeof(b),
            "<div class=qcard><div class=qh><span class=qnum>#08</span><span class=qt>Field Device Recovery</span></div>"
            "<p>This one is completed over <b>SNMP (UDP 161)</b>, not a web form: restore RTU-07 to its "
            "last-good firmware using the runbook (<a href=\"/evidence?f=rtu07_recovery\">rtu07_recovery</a>). "
            "Once it reads healthy, the badge issues the flag.</p>"
            "<p>Live device status: %s</p></div>", st);
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
