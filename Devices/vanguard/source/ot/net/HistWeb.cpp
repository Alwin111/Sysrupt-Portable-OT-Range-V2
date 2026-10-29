// SPDX-License-Identifier: Apache-2.0
#include "HistWeb.h"
#include "OtHttp.h"  // shared web design system (ot::net::kWebStyle)

#include "../sim/HistSqlUnion.h"

#include <tactility/log.h>

#include <esp_http_server.h>
#include <esp_random.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The process historian console, presented as a SEPARATE internal system (HIST-04) with its OWN look
// and its OWN access gate (login "historian"/"readonly", discovered via /hist-config - a different
// OSINT path than the corporate /control note). It lives under the /hist path, off the corporate
// nav, discovered via the mail threads (soc@/eng@ point to 10.0.1.1/hist).
//
// NOTE: this was originally a second esp_http_server on :8443, but on this no-PSRAM board a second
// server instance left free internal heap at ~5-6 KB and destabilised the Wi-Fi AP under load. It is
// therefore served as a set of routes on the SINGLE :80 server instead (its own login + styling keep
// the "separate system" feel) - recovering the ~11 KB the second instance cost.
namespace ot::net {

namespace {

constexpr auto* TAG = "HistWeb";

// Weak, read-only historian credential - deliberately NOT the corporate portal login, discovered via a
// DIFFERENT OSINT path (/hist-config), not the corporate robots -> /control path.
constexpr auto* HIST_USER = "historian";
constexpr auto* HIST_PASS = "readonly";

// ---- tiny session table (separate from the :80 corporate sessions) ----
struct HSession { char token[17]; bool used; };
HSession g_sessions[4];
int g_sessNext = 0;
SemaphoreHandle_t g_mutex = nullptr;

void lk() { if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY); }
void ulk() { if (g_mutex) xSemaphoreGive(g_mutex); }

void makeToken(char out[17]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) out[i] = hex[esp_random() & 0xF];
    out[16] = '\0';
}

void addSession(char out[17]) {
    makeToken(out);
    lk();
    HSession& s = g_sessions[g_sessNext];
    g_sessNext = (g_sessNext + 1) % (int)(sizeof(g_sessions) / sizeof(g_sessions[0]));
    std::strncpy(s.token, out, sizeof(s.token));
    s.used = true;
    ulk();
}

bool validSession(httpd_req_t* req) {
    char cookie[160];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) return false;
    const char* p = std::strstr(cookie, "vghist=");
    if (!p) return false;
    p += 7;
    char token[17] = {};
    int i = 0;
    while (i < 16 && p[i] && p[i] != ';' && p[i] != ' ') { token[i] = p[i]; i++; }
    bool found = false;
    lk();
    for (auto& s : g_sessions) if (s.used && std::strncmp(s.token, token, 16) == 0) { found = true; break; }
    ulk();
    return found;
}

void noKeepAlive(httpd_req_t* req) { httpd_resp_set_hdr(req, "Connection", "close"); }

int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 0x20);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

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
        else if (c == '%' && p[1] && p[2] && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { c = (char)(hexv(p[1]) * 16 + hexv(p[2])); p += 3; }
        else p++;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

bool queryVal(httpd_req_t* req, const char* key, char* out, size_t cap) {
    out[0] = '\0';
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1 || qlen > 400) return false;
    char q[400];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, cap) == ESP_OK;
}

void urlDecode(char* s) {
    char* o = s;
    for (char* p = s; *p;) {
        if (*p == '%' && p[1] && p[2] && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { *o++ = (char)(hexv(p[1]) * 16 + hexv(p[2])); p += 3; }
        else if (*p == '+') { *o++ = ' '; p++; }
        else { *o++ = *p++; }
    }
    *o = '\0';
}

// Distinct "data system" styling (slate + teal), deliberately different from the corporate portal.
// The historian console uses the shared SYSRUPT design system (ot::net::kWebStyle, OtHttp.h) so it
// matches the rest of the web HMI instead of its own terminal-teal palette.
constexpr auto* HSTYLE = ot::net::kWebStyle;

esp_err_t sendLogin(httpd_req_t* req, const char* err) {
    noKeepAlive(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><title>HIST-04 Historian Console</title>");
    httpd_resp_sendstr_chunk(req, HSTYLE);
    httpd_resp_sendstr_chunk(req,
        "<style>:root{--nav:#3a4a63;--brown:#334156;--txt:#2d3a4d;--mut:#5a6a7d;--acc:#3a5a85}</style>"
        "</head><body><div class=login><div class=card>"
        "<h1>HIST-04 &middot; PROCESS HISTORIAN</h1>"
        "<div class=sub>internal data system &mdash; read-only console</div>"
        "<form method=POST action=/hist/login class=box>"
        "<label>User</label><input name=user autocomplete=off autocapitalize=off>"
        "<label>Password</label><input name=pass type=password autocomplete=off>"
        "<button type=submit>Connect</button>");
    if (err) {
        char e[128];
        std::snprintf(e, sizeof(e), "<div class=warn style='margin-top:8px'>%s</div>", err);
        httpd_resp_sendstr_chunk(req, e);
    }
    httpd_resp_sendstr_chunk(req,
        "</form><div class=foot>HIST-04 &middot; sysrupt-historian v1.9.7 &middot; internal use only</div>"
        "</div></div></body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t handleRoot(httpd_req_t* req) {
    if (!validSession(req)) return sendLogin(req, nullptr);
    noKeepAlive(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><title>HIST-04 Historian Console</title>");
    httpd_resp_sendstr_chunk(req, HSTYLE);
    httpd_resp_sendstr_chunk(req,
        // Historian is a SEPARATE internal system (discovered via /hist, not advertised in the plant
        // nav): its own header identity and its own dark-blue accent (distinct from the plant brown).
        // The shared .topbar background is a hardcoded brown gradient, so override it here to dark blue
        // so the historian's top bar matches its slate-blue identity instead of the corporate brown.
        "<style>:root{--nav:#334156;--brown:#334156;--txt:#2d3a4d;--mut:#5a6a7d;--acc:#3a5a85}"
        ".topbar{background:linear-gradient(180deg,#3a4a63,#2d3a4d)}</style>"
        "</head><body>"
        "<header class=topbar><div class=topbar-inner>"
        "<span class=brand>HIST-04<i>Process Historian &middot; Unit 1</i></span>"
        "<nav><a href=/intranet>&larr; Plant</a></nav>"
        "</div></header>"
        "<h1>Process Historian</h1>"
        "<div class=sub>HIST-04 &middot; connected &middot; engine: sysrupt-historian (read-only)</div>"
        // The "Filter by Source" panel -> /hist/analyze (sim/HistSqlUnion). The source field
        // concatenates user input straight into the backend query, so it is SQL-injectable: this is
        // the #07 Historian SQL Injection challenge. It is the only data interface the console exposes.
        "<div class=box><b>PROCESS DATA &middot; FILTER BY SOURCE</b>\nQuery recorded process data by source.\n"
        "<label>source</label><input id=src placeholder=\"intake\" spellcheck=false>"
        "<button id=sb>Query</button>"
        "<div id=sst class=sub style='margin-top:6px'></div><div id=sres></div></div>");
    httpd_resp_sendstr_chunk(req,
        "<script>(function(){"
        "function tbl(cols,rows){var t=document.createElement('table'),h=document.createElement('tr');"
        "cols.forEach(function(c){var th=document.createElement('th');th.textContent=c;h.appendChild(th);});"
        "t.appendChild(h);rows.forEach(function(r){var tr=document.createElement('tr');"
        "r.forEach(function(v){var td=document.createElement('td');td.textContent=v;tr.appendChild(td);});"
        "t.appendChild(tr);});return t;}"
        "var src=document.getElementById('src'),sst=document.getElementById('sst'),sres=document.getElementById('sres');"
        "async function ana(){sst.textContent='Running\\u2026';sst.className='';sres.textContent='';"
        "try{var r=await fetch('/hist/analyze?source='+encodeURIComponent(src.value),{cache:'no-store'});var d=await r.json();"
        "if(d.error){sst.textContent='Error: '+d.error;sst.className='warn';}"
        "else{sres.appendChild(tbl(d.columns||[],d.rows||[]));"
        "sst.textContent='Rows: '+d.rowCount+'  \\u00b7  '+d.execMs+' ms';sst.className='ok';}}"
        "catch(e){sst.textContent='Query failed (no response).';sst.className='warn';}}"
        "document.getElementById('sb').onclick=ana;"
        "src.addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();ana();}});"
        "})();</script></body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t handleLogin(httpd_req_t* req) {
    char body[192] = {};
    int total = 0, r;
    while ((r = httpd_req_recv(req, body + total, sizeof(body) - 1 - total)) > 0) {
        total += r;
        if (total >= (int)sizeof(body) - 1) break;
    }
    char user[48] = {}, pass[48] = {};
    formField(body, "user", user, sizeof(user));
    formField(body, "pass", pass, sizeof(pass));
    if (std::strcmp(user, HIST_USER) == 0 && std::strcmp(pass, HIST_PASS) == 0) {
        char token[17];
        addSession(token);
        char cookie[64];
        std::snprintf(cookie, sizeof(cookie), "vghist=%s; Path=/", token);
        noKeepAlive(req);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/hist");
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
        return httpd_resp_send(req, nullptr, 0);
    }
    return sendLogin(req, "Access denied.");
}

// The historian's own exposed config (a different OSINT path than the corporate /control note):
// discloses the weak default read-only credential.
esp_err_t handleConfig(httpd_req_t* req) {
    noKeepAlive(req);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req,
        "# sysrupt-historian (HIST-04) - service config (read-only console at /hist)\n"
        "# WARNING: default commissioning credential never rotated (finding IS-SEC-021).\n"
        "console       = http://10.0.1.1/hist\n"
        "auth.mode     = basic\n"
        "auth.user     = historian\n"
        "auth.password = readonly\n"
        "query.engine  = sysrupt-historian (Filter-by-Source query)\n");
}

// /hist/analyze?source=<input> -> the Filter-by-Source SQL-injection interface (sim/HistSqlUnion).
// This is a bounded pattern matcher, NOT SQLite. The source field concatenates user input straight
// into the backend query, so UNION-based injection extracts the #07 flag from the flags table; the
// flag is validated when the player submits it on the corporate portal (ChKind::Forensics).
esp_err_t handleAnalyze(httpd_req_t* req) {
    if (!validSession(req)) {
        noKeepAlive(req);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"not authenticated\",\"rowCount\":0,\"execMs\":0}");
    }
    noKeepAlive(req);
    httpd_resp_set_type(req, "application/json");

    char arg[200] = {};
    queryVal(req, "source", arg, sizeof(arg));   // an empty/absent source is a valid (benign) filter
    urlDecode(arg);

    // The simulator's largest response (the 6-row credentials dump) is ~811 bytes, so 1 KB is enough.
    // Kept small on purpose: this request can run while a challenge LCD view is open and internal heap
    // is tight on this no-PSRAM board, so a 2 KB transient alloc risked pushing it over the edge.
    const size_t BUFSZ = 1024;
    char* buf = (char*)std::malloc(BUFSZ);
    if (!buf) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"error\":\"low memory - retry\",\"rowCount\":0,\"execMs\":0}");
    }
    size_t n = ot::sim::histsqlunion::evaluate(arg, std::strlen(arg), buf, BUFSZ);
    esp_err_t rc = httpd_resp_send(req, buf, n);
    std::free(buf);
    return rc;
}

httpd_uri_t mk(const char* path, httpd_method_t m, esp_err_t (*fn)(httpd_req_t*)) {
    httpd_uri_t u = {};
    u.uri = path; u.method = m; u.handler = fn;
    return u;
}

} // namespace

void registerHistorianRoutes(httpd_handle_t server) {
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
    for (auto& s : g_sessions) s.used = false;
    const httpd_uri_t uris[] = {
        mk("/hist", HTTP_GET, handleRoot),
        mk("/hist/login", HTTP_POST, handleLogin),
        mk("/hist/analyze", HTTP_GET, handleAnalyze),
        mk("/hist-config", HTTP_GET, handleConfig),
    };
    for (const auto& u : uris) httpd_register_uri_handler(server, &u);
    LOG_I(TAG, "Historian console routes up on http://10.0.1.1/hist (login historian/readonly via /hist-config)");
}

} // namespace ot::net
