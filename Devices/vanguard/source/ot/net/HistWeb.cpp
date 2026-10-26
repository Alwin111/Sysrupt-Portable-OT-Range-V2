// SPDX-License-Identifier: Apache-2.0
#include "HistWeb.h"

#include "../HistorianService.h"
#include "../sim/HistSql.h"
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
// OSINT path than the corporate /onboarding note). It lives under the /hist path, off the corporate
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
// DIFFERENT OSINT path (/hist-config), not the corporate robots -> /onboarding path.
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
constexpr auto* HSTYLE =
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>:root{--bg:#0b1414;--pnl:#10201f;--line:#1f3b38;--tl:#2fd4c4;--txt:#ddeceb;--mut:#6f8b88;--amb:#e0b34a;--red:#e8644c}"
    "*{box-sizing:border-box}"
    "body{background:var(--bg);color:var(--txt);font-family:ui-monospace,Menlo,Consolas,monospace;font-size:14px;"
    "line-height:1.55;margin:0 auto;padding:22px;max-width:960px}"
    "h1{color:var(--tl);font-size:20px;letter-spacing:1px;margin:0 0 4px}"
    ".sub{color:var(--mut);margin-bottom:14px;font-size:13px}"
    "a{color:var(--tl)}"
    ".box{border:1px solid var(--line);background:var(--pnl);border-radius:8px;padding:14px;margin:10px 0;white-space:pre-wrap}"
    "input,textarea{width:100%;background:#071010;border:1px solid var(--line);color:var(--txt);"
    "font-family:inherit;font-size:14px;padding:9px 10px;border-radius:6px}"
    "textarea{min-height:64px;resize:vertical}"
    "button{background:var(--tl);color:#042220;border:0;padding:9px 18px;font-weight:700;border-radius:6px;cursor:pointer;margin-top:10px}"
    "button:hover{filter:brightness(1.08)}"
    "label{display:block;color:var(--mut);font-size:12px;margin:10px 0 4px}"
    "table{border-collapse:collapse;width:100%;margin:8px 0;display:block;overflow-x:auto;font-size:13px}"
    "td,th{border:1px solid var(--line);padding:6px 10px;text-align:left;white-space:nowrap}"
    "th{color:var(--amb);background:#0c1a19}"
    "#st{min-height:18px;margin:6px 0;font-size:13px}.ok{color:var(--tl)}.warn{color:var(--red)}"
    ".login{min-height:80vh;display:flex;flex-direction:column;align-items:center;justify-content:center}"
    ".login .card{max-width:360px;width:100%}"
    ".foot{color:#365351;font-size:10px;margin-top:12px;letter-spacing:.5px}"
    "@media(max-width:600px){body{padding:14px}}</style>";

esp_err_t sendLogin(httpd_req_t* req, const char* err) {
    noKeepAlive(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><title>HIST-04 Historian Console</title>");
    httpd_resp_sendstr_chunk(req, HSTYLE);
    httpd_resp_sendstr_chunk(req,
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
        "</head><body>"
        "<h1>HIST-04 &middot; PROCESS HISTORIAN</h1>"
        "<div class=sub>connected &middot; engine: sysrupt-historian (read-only)</div>"
        // Read-only process-data dashboard: recent CLEAN samples for a chosen tag, a sequence-gap
        // hint, and an on-demand audit trail. Populated via the existing /hist/query engine; it only
        // ever reads the clean `samples` and `audit` views, so deleted rows / the recovered value are
        // never shown here. Defaults to the first tag (a decoy), so the tamper tag is not pre-selected.
        "<div class=box><b>PROCESS DATA</b>\nRecent committed samples for a tag (clean history, read-only).\n"
        "<label>tag</label><select id=dtag></select>"
        "<div id=dgap class=sub style='margin-top:6px'></div>"
        "<div id=drows></div>"
        "<button id=ab style='margin-top:10px'>Load audit trail</button>"
        "<div id=daudit style='margin-top:8px'></div></div>"
        // Tag filter -> ?search= (the concatenated/injectable backend path; behavior unchanged).
        "<div class=box><b>TAG SEARCH</b>\nFilter committed samples by tag name.\n"
        "<label>tag</label><input id=tg placeholder=\"PUMP.FLOW\" spellcheck=false>"
        "<button id=tb>Search</button></div>"
        // The read-only SQL console.
        "<div class=box><b>SQL QUERY</b>\nRead-only, streaming query console.\n"
        "<textarea id=q maxlength=150 spellcheck=false placeholder=\"SHOW TABLES\"></textarea>"
        "<button id=rb>Run</button>"
        "<div class=sub style='margin-top:6px'>Supported: SHOW TABLES &middot; DESCRIBE &lt;t&gt; &middot; "
        "SELECT col[,col]|* FROM &lt;t&gt; [WHERE ...] [LIMIT n] &middot; no UNION / ORDER BY / DISTINCT / JOIN</div>"
        "<div class=sub>Examples: <code>SHOW TABLES</code> &middot; <code>DESCRIBE samples</code> &middot; "
        "<code>SELECT seq,value FROM samples WHERE tag='PUMP.FLOW' LIMIT 10</code> &middot; <code>SELECT line FROM audit</code></div>"
        "</div>"
        // Offensive, UNSCORED "Filter by Source" panel -> /hist/analyze (sim/HistSqlUnion, a bounded
        // SQL-injection SIMULATOR, NOT SQLite). It shares no state with the scored forensic engine and
        // never affects a challenge solve; it exists so players can discover the V1 historian SQLi.
        "<div class=box><b>PROCESS DATA &middot; FILTER BY SOURCE</b>\nQuery recorded process data by source.\n"
        "<label>source</label><input id=src placeholder=\"intake\" spellcheck=false>"
        "<button id=sb>Query</button>"
        "<div id=sst class=sub style='margin-top:6px'></div><div id=sres></div></div>"
        "<div id=st></div><div id=res></div>");
    httpd_resp_sendstr_chunk(req,
        "<script>(function(){"
        "var q=document.getElementById('q'),tg=document.getElementById('tg'),"
        "st=document.getElementById('st'),res=document.getElementById('res'),"
        "dtag=document.getElementById('dtag'),drows=document.getElementById('drows'),"
        "dgap=document.getElementById('dgap'),daudit=document.getElementById('daudit');"
        "function tbl(cols,rows){var t=document.createElement('table'),h=document.createElement('tr');"
        "cols.forEach(function(c){var th=document.createElement('th');th.textContent=c;h.appendChild(th);});"
        "t.appendChild(h);rows.forEach(function(r){var tr=document.createElement('tr');"
        "r.forEach(function(v){var td=document.createElement('td');td.textContent=v;tr.appendChild(td);});"
        "t.appendChild(tr);});return t;}"
        "async function jq(sql){var r=await fetch('/hist/query?sql='+encodeURIComponent(sql),{cache:'no-store'});return await r.json();}"
        "async function go(url){st.textContent='Running\\u2026';st.className='';res.textContent='';"
        "try{var r=await fetch(url,{cache:'no-store'});var d=await r.json();"
        "if(d.error){st.textContent='Error: '+d.error;st.className='warn';}"
        "else{res.appendChild(tbl(d.columns||[],d.rows||[]));"
        "st.textContent='Rows: '+d.rowCount+'  \\u00b7  '+d.execMs+' ms';st.className='ok';}}"
        "catch(e){st.textContent='Query failed (no response).';st.className='warn';}}"
        // Dashboard: clean samples for the selected tag + a sequence-gap hint (streamed, bounded).
        "async function samples(t){dgap.textContent='';drows.textContent='';"
        "try{var d=await jq(\"SELECT seq,value FROM samples WHERE tag='\"+t+\"' LIMIT 20\");"
        "if(d.error){drows.textContent='Error: '+d.error;return;}"
        "drows.appendChild(tbl(d.columns||[],d.rows||[]));"
        "var s=(d.rows||[]).map(function(r){return parseInt(r[0],10);}).filter(function(n){return !isNaN(n);});"
        "s.sort(function(a,b){return a-b;});var g=[];for(var i=1;i<s.length;i++){for(var k=s[i-1]+1;k<s[i];k++)g.push(k);}"
        "dgap.innerHTML=g.length?('<span class=warn>sequence gap: missing seq '+g.join(', ')+'</span>'):'no gaps in the committed series';}"
        "catch(e){drows.textContent='Query failed.';}}"
        "async function loadTags(){try{var d=await jq('SELECT name FROM tags');"
        "(d.rows||[]).forEach(function(r){var o=document.createElement('option');o.textContent=r[0];dtag.appendChild(o);});"
        "if(dtag.options.length)samples(dtag.value);}catch(e){}}"
        // Audit trail loaded only on demand (an investigation clue, not auto-revealed).
        "async function audit(){daudit.textContent='Loading\\u2026';"
        "try{var d=await jq('SELECT line FROM audit LIMIT 20');daudit.textContent='';"
        "daudit.appendChild(tbl(d.columns||[],d.rows||[]));}catch(e){daudit.textContent='Query failed.';}}"
        "dtag.onchange=function(){samples(dtag.value);};"
        "document.getElementById('ab').onclick=audit;"
        "document.getElementById('rb').onclick=function(){var s=q.value.trim();"
        "if(!s){st.textContent='Enter a query.';st.className='warn';return;}go('/hist/query?sql='+encodeURIComponent(s));};"
        "document.getElementById('tb').onclick=function(){go('/hist/query?search='+encodeURIComponent(tg.value));};"
        "q.addEventListener('keydown',function(e){if((e.ctrlKey||e.metaKey)&&e.key==='Enter'){e.preventDefault();document.getElementById('rb').click();}});"
        "tg.addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();document.getElementById('tb').click();}});"
        // Filter-by-Source (offensive, unscored): renders into its own status/result area (sst/sres).
        "var src=document.getElementById('src'),sst=document.getElementById('sst'),sres=document.getElementById('sres');"
        "async function ana(){sst.textContent='Running\\u2026';sst.className='';sres.textContent='';"
        "try{var r=await fetch('/hist/analyze?source='+encodeURIComponent(src.value),{cache:'no-store'});var d=await r.json();"
        "if(d.error){sst.textContent='Error: '+d.error;sst.className='warn';}"
        "else{sres.appendChild(tbl(d.columns||[],d.rows||[]));"
        "sst.textContent='Rows: '+d.rowCount+'  \\u00b7  '+d.execMs+' ms';sst.className='ok';}}"
        "catch(e){sst.textContent='Query failed (no response).';sst.className='warn';}}"
        "document.getElementById('sb').onclick=ana;"
        "src.addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();ana();}});"
        "loadTags();"
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

// The historian's own exposed config (a different OSINT path than the corporate onboarding note):
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
        "query.engine  = sysrupt-historian (read-only streaming SQL)\n");
}

// /hist/query?sql=<stmt>  -> safe SQL engine ; /hist/query?search=<tag> -> vulnerable concatenated tag search.
esp_err_t handleQuery(httpd_req_t* req) {
    if (!validSession(req)) {
        noKeepAlive(req);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"not authenticated\",\"rowCount\":0,\"execMs\":0}");
    }
    noKeepAlive(req);
    httpd_resp_set_type(req, "application/json");

    char arg[200];
    bool isSearch = false;
    if (queryVal(req, "search", arg, sizeof(arg))) { isSearch = true; }
    else if (!queryVal(req, "sql", arg, sizeof(arg)) || !arg[0]) {
        return httpd_resp_sendstr(req, "{\"error\":\"empty query\",\"rowCount\":0,\"execMs\":0}");
    }
    urlDecode(arg);

    const size_t BUFSZ = 2048;
    char* buf = (char*)std::malloc(BUFSZ);
    if (!buf) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"error\":\"low memory - retry\",\"rowCount\":0,\"execMs\":0}");
    }
    size_t n;
    auto& hs = ot::HistorianService::instance();
    hs.markTouched();
    if (hs.lock()) {
        if (isSearch) n = ot::sim::histsql::searchTag(hs.system(), arg, std::strlen(arg), buf, BUFSZ);
        else          n = ot::sim::histsql::execute(hs.system(), arg, std::strlen(arg), buf, BUFSZ);
        hs.unlock();
    } else {
        n = (size_t)std::snprintf(buf, BUFSZ,
            "{\"error\":\"historian unavailable - open the Historian challenge on the badge, then retry\","
            "\"rowCount\":0,\"execMs\":0}");
    }
    esp_err_t rc = httpd_resp_send(req, buf, n);
    std::free(buf);
    return rc;
}

// /hist/analyze?source=<input> -> the OFFENSIVE, UNSCORED Filter-by-Source SQL-injection SIMULATOR
// (sim/HistSqlUnion). This is a bounded pattern matcher, NOT SQLite. It is deliberately independent of
// the forensic /hist/query engine: it holds no HistorianService state, needs no lock, and can never
// latch a challenge solve - so defensive #07/#09 are unaffected by anything that happens here.
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
        mk("/hist/query", HTTP_GET, handleQuery),
        mk("/hist/analyze", HTTP_GET, handleAnalyze),
        mk("/hist-config", HTTP_GET, handleConfig),
    };
    for (const auto& u : uris) httpd_register_uri_handler(server, &u);
    LOG_I(TAG, "Historian console routes up on http://10.0.1.1/hist (login historian/readonly via /hist-config)");
}

} // namespace ot::net
