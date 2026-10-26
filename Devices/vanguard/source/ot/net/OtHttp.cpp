// SPDX-License-Identifier: Apache-2.0
#include "OtHttp.h"

#include "../OtService.h"
#include "../sim/PlantModel.h"
#include "DefWeb.h"
#include "HistWeb.h"
#include "ReactorImg.h"
#include "ToolkitZip.h"

#include <tactility/log.h>

#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_random.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstring>

// The intentional engineering HMI for the OT range (port 80). Designed challenge entry:
//   /            -> engineer console login
//   /robots.txt  -> leaks Disallow: /control
//   /control     -> an engineering maintenance notice (authentic plant text) that discloses the
//                   temporary engineer credentials and documents the rod-drive maintenance rules
//   /login       -> authenticates and opens /hmi
//   /hmi         -> live reactor + maintenance HMI (gauges, supervisor unlock, last-authorised,
//                   unlock expiry, and a "Step one done" indicator). Control is over Modbus 502.
// This is the range's own HTTP server; Tactility's filesystem web server stays disabled.
namespace ot::net {

namespace {

constexpr auto* TAG = "OtHttp";

// Temporary engineer console credentials, disclosed in the /control engineering note.
constexpr auto* ENG_USER = "eng";
constexpr auto* ENG_PASS = "Rec0ncile!42";

// ---- Tiny in-memory session table ----
struct Session {
    char token[17];
    bool used;
};
Session g_sessions[8];
int g_sessNext = 0;
SemaphoreHandle_t g_sessMutex = nullptr;

void sessLock() { if (g_sessMutex) xSemaphoreTake(g_sessMutex, portMAX_DELAY); }
void sessUnlock() { if (g_sessMutex) xSemaphoreGive(g_sessMutex); }

void makeToken(char out[17]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i] = hex[esp_random() & 0xF];
    }
    out[16] = '\0';
}

void addSession(char out[17]) {
    makeToken(out);
    sessLock();
    Session& s = g_sessions[g_sessNext];
    g_sessNext = (g_sessNext + 1) % (int)(sizeof(g_sessions) / sizeof(g_sessions[0]));
    std::strncpy(s.token, out, sizeof(s.token));
    s.used = true;
    sessUnlock();
}

bool validSession(httpd_req_t* req) {
    char cookie[160];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }
    const char* p = std::strstr(cookie, "vgsess=");
    if (p == nullptr) {
        return false;
    }
    p += 7;
    char token[17] = {};
    int i = 0;
    while (i < 16 && p[i] && p[i] != ';' && p[i] != ' ') {
        token[i] = p[i];
        i++;
    }
    bool found = false;
    sessLock();
    for (auto& s : g_sessions) {
        if (s.used && std::strncmp(s.token, token, 16) == 0) {
            found = true;
            break;
        }
    }
    sessUnlock();
    return found;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 0x20);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Extracts a form field and URL-decodes it (%XX and '+'), since browsers percent-encode the
// application/x-www-form-urlencoded body (e.g. '!' -> %21 in a password).
bool formField(const char* body, const char* key, char* out, size_t cap) {
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char* p = std::strstr(body, needle);
    if (p == nullptr) {
        return false;
    }
    p += std::strlen(needle);
    size_t n = 0;
    while (*p && *p != '&' && n + 1 < cap) {
        char c = *p;
        if (c == '+') {
            c = ' ';
            p++;
        } else if (c == '%' && p[1] && p[2] && hexVal(p[1]) >= 0 && hexVal(p[2]) >= 0) {
            c = (char)(hexVal(p[1]) * 16 + hexVal(p[2]));
            p += 3;
        } else {
            p++;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

// Shared design tokens mirror the badge LCD palette (ui/Theme.h) so the web HMI and the on-device
// screens read as one product: matte-black ground, near-white body text, red offensive accent, neon
// green for OK/action, amber gauge read-outs, rounded panels. The viewport meta makes it usable on a
// phone. Kept compact on purpose - the device serves these pages from a very small heap budget.
// Shared design system (reactor/engineering "red" domain). Sans-serif for prose + labels, monospace
// for data/telemetry/registers; one restrained palette; consistent controls, focus states and spacing.
// DefWeb uses the same system with the intranet "blue" accent so the two domains read as one product.
constexpr auto* STYLE =
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>:root{--bg:#07090B;--pnl:#0E1113;--line:#5C1E18;--red:#E8593C;--acc:#FF3B30;"
    "--grn:#3DFF8F;--txt:#EEF4F6;--mut:#9BA4A8;--amb:#FFC14D;"
    "--mono:ui-monospace,'SFMono-Regular',Menlo,Consolas,monospace;"
    "--sans:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,system-ui,sans-serif}"
    "*{box-sizing:border-box}"
    "body{background:var(--bg);color:var(--txt);font-family:var(--sans);font-size:15px;line-height:1.55;"
    "margin:0 auto;padding:24px;max-width:1150px}"
    "h1{color:var(--red);letter-spacing:2px;margin:0 0 10px;font-size:25px;font-weight:700;"
    "border-bottom:1px solid var(--line);padding-bottom:8px}"
    ".sub{color:var(--mut);margin-bottom:16px;font-size:14px}"
    "code,pre,.box,td,th,input,.g b,.statpill,.mono{font-family:var(--mono)}"
    "input{background:var(--pnl);border:1px solid var(--line);color:var(--txt);"
    "font-size:16px;padding:10px 11px;margin:6px 0;width:340px;max-width:100%;border-radius:6px}"
    "button{background:var(--grn);color:#042012;border:0;padding:11px 22px;font-family:var(--sans);"
    "font-size:15px;font-weight:700;cursor:pointer;margin-top:12px;border-radius:6px}"
    "button:hover{filter:brightness(1.08)}"
    "a:focus-visible,button:focus-visible,input:focus-visible{outline:2px solid var(--amb);outline-offset:2px}"
    ".box{border:1px solid var(--line);background:var(--pnl);padding:16px;white-space:pre-wrap;"
    "border-radius:10px;font-size:14px;line-height:1.5}"
    ".login{min-height:82vh;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center}"
    ".login h1{font-size:30px;border:0}.login .box{max-width:440px;width:100%;text-align:left}"
    ".g{display:inline-block;border:1px solid var(--line);background:var(--pnl);padding:12px 18px;margin:5px;"
    "min-width:150px;border-radius:8px;vertical-align:top}"
    ".g b{color:var(--amb);display:block;font-size:26px;margin-top:2px}"
    ".g span{color:var(--mut);font-size:12px;letter-spacing:1px}"
    ".warn{color:var(--acc)}.ok{color:var(--grn)}a{color:var(--grn)}a:hover{text-decoration:underline}"
    "@media(max-width:600px){body{padding:16px}h1{font-size:21px}.g{min-width:44%}}</style>";

esp_err_t redirect(httpd_req_t* req, const char* location, const char* setCookie) {
    setNoKeepAlive(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    if (setCookie) {
        httpd_resp_set_hdr(req, "Set-Cookie", setCookie);
    }
    return httpd_resp_send(req, nullptr, 0);
}

// region handlers

// The corporate portal login - a deliberately plain, "boring and real" internal-tool gate (NOT the
// red/terminal badge aesthetic). No challenge flavour, no hint that it is crackable; credentials are
// discovered off-page via robots.txt -> /onboarding. A wrong login returns a single generic error
// (no username enumeration). `err` is shown under the card when non-null.
esp_err_t sendCorpLogin(httpd_req_t* req, const char* err) {
    setNoKeepAlive(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><title>Sysrupt Industrial Systems</title>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<style>*{box-sizing:border-box}"
        "body{margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;"
        "background:#11151c;color:#c9d1d9;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,system-ui,sans-serif;padding:24px}"
        ".card{background:#1b2129;border:1px solid #2a323c;border-radius:8px;padding:28px 26px;width:100%;max-width:380px;box-shadow:0 6px 24px rgba(0,0,0,.35)}"
        ".wm{font-size:15px;font-weight:600;letter-spacing:.3px;color:#e6edf3;margin-bottom:2px}"
        ".wm span{color:#5b6673;font-weight:400}"
        ".ttl{font-size:12px;color:#5b6673;margin-bottom:18px}"
        "label{display:block;font-size:12px;color:#8b949e;margin:12px 0 4px}"
        "input{width:100%;background:#0d1117;border:1px solid #2a323c;border-radius:5px;color:#e6edf3;padding:9px 10px;font-size:14px}"
        "input:focus{outline:2px solid #2f6feb;border-color:#2f6feb}"
        "button{width:100%;margin-top:18px;background:#2f6feb;color:#fff;border:0;border-radius:5px;padding:10px;font-size:14px;font-weight:600;cursor:pointer}"
        "button:hover{filter:brightness(1.08)}"
        ".err{color:#f0883e;font-size:13px;margin-top:12px;min-height:16px}"
        ".banner{max-width:380px;font-size:11px;color:#5b6673;margin-top:16px;text-align:center;line-height:1.55}"
        ".foot{font-size:10px;color:#3d444d;margin-top:14px;letter-spacing:.5px}"
        "</style></head><body>"
        "<form class=card method=POST action=/login>"
        "<div class=wm>Sysrupt <span>Industrial Systems</span></div>"
        "<div class=ttl>Internal Portal</div>"
        "<label>Username</label><input name=user autocomplete=off autocapitalize=off autocorrect=off>"
        "<label>Password</label><input name=pass type=password autocomplete=off>"
        "<button type=submit>Sign In</button>");
    if (err) {
        char e[160];
        snprintf(e, sizeof(e), "<div class=err>%s</div>", err);
        httpd_resp_sendstr_chunk(req, e);
    }
    httpd_resp_sendstr_chunk(req,
        "</form>"
        "<div class=banner>This system is for authorized Sysrupt Industrial Systems personnel only. "
        "All access is logged and monitored. Unauthorized use is prohibited.</div>"
        "<div class=foot>IR-PORTAL v2.3 &middot; internal use only</div>"
        "</body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t handleRoot(httpd_req_t* req) {
    return sendCorpLogin(req, nullptr);
}

esp_err_t handleLogin(httpd_req_t* req) {
    char body[256] = {};
    int total = 0, r;
    while ((r = httpd_req_recv(req, body + total, sizeof(body) - 1 - total)) > 0) {
        total += r;
        if (total >= (int)sizeof(body) - 1) break;
    }
    char user[48] = {}, pass[48] = {};
    formField(body, "user", user, sizeof(user));
    formField(body, "pass", pass, sizeof(pass));
    if (std::strcmp(user, ENG_USER) == 0 && std::strcmp(pass, ENG_PASS) == 0) {
        char token[17];
        addSession(token);
        char cookie[64];
        snprintf(cookie, sizeof(cookie), "vgsess=%s; Path=/", token);
        // Land on the portal Home (plant status + IR workspace), not the reactor HMI.
        return redirect(req, "/intranet", cookie);
    }
    // Generic error - no username enumeration ("user not found" vs "wrong password").
    return sendCorpLogin(req, "Invalid username or password.");
}

// The reactor vessel artwork for the HMI left panel (decorative). Not session-gated: it is just an
// image (no challenge data), cached by the browser so it loads once.
esp_err_t handleReactorPng(httpd_req_t* req) {
    setNoKeepAlive(req);
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char*)reactor_png, reactor_png_len);
}

// /toolkit.zip: the player-side CLI toolkit release archive, gated behind the SAME engineer session
// as /hmi. The bytes live in flash .rodata (ToolkitZip.cpp); we stream them to the socket in small
// chunks so we never allocate a second copy of the ~54 KB archive in the badge's tiny heap. No flag,
// credential or challenge answer is in the archive or in any response here.
esp_err_t handleToolkitDownload(httpd_req_t* req) {
    setNoKeepAlive(req);
    if (!validSession(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "401 Unauthorized - log in at / first\n");
    }
    httpd_resp_set_type(req, "application/zip");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"sysrupt-toolkit.zip\"");
    httpd_resp_set_hdr(req, "X-Toolkit-SHA256", toolkit_zip_sha256);
    const unsigned int CHUNK = 1024;
    for (unsigned int off = 0; off < toolkit_zip_len; off += CHUNK) {
        unsigned int n = (toolkit_zip_len - off) < CHUNK ? (toolkit_zip_len - off) : CHUNK;
        if (httpd_resp_send_chunk(req, (const char*)(toolkit_zip + off), n) != ESP_OK) {
            return ESP_FAIL; // client dropped: stop streaming
        }
    }
    return httpd_resp_send_chunk(req, nullptr, 0); // terminate the chunked response
}

esp_err_t handleRobots(httpd_req_t* req) {
    setNoKeepAlive(req);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "User-agent: *\nDisallow: /control\nDisallow: /onboarding\nDisallow: /hist-config\n");
}

// /onboarding: an internal IR-portal onboarding note. This is the OSINT path that discloses the
// portal credentials (reached via robots.txt), keeping the login page itself a clean, honest-looking
// gate with no visible tell.
esp_err_t handleOnboarding(httpd_req_t* req) {
    setNoKeepAlive(req);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req,
        "Sysrupt Industrial Systems - IR Portal onboarding (internal)\n"
        "IR-PORTAL v2.3\n"
        "\n"
        "Welcome to the incident-response portal. For the duration of this engagement the IR team\n"
        "uses a single shared portal account:\n"
        "\n"
        "    username: eng\n"
        "    password: Rec0ncile!42\n"
        "\n"
        "Sign in at the portal root ( / ). Rotate the shared credential after sign-off per policy\n"
        "IS-SEC-014. Do NOT forward this note outside the IR team.\n");
}

// /control: the engineering maintenance notice. Authentic-looking plant text that (a) discloses the
// temporary engineer credentials and (b) documents the rod-drive maintenance rules the player then
// exploits over Modbus.
esp_err_t handleControl(httpd_req_t* req) {
    setNoKeepAlive(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><title>Engineering Notice</title>");
    httpd_resp_sendstr_chunk(req, STYLE);
    httpd_resp_sendstr_chunk(req,
        "</head><body><h1>ENGINEERING MAINTENANCE NOTICE</h1>"
        "<div class=sub>SYSRUPT NUCLEAR &mdash; OT-CTRL / Unit-1 / Rev 7 (CONTROLLED COPY)</div>"
        "<div class=box>"
        "SUBJECT: Control-rod drive servicing via the supervisory HMI\n\n"
        "1. SAFETY INTERLOCK. The maintenance rod drive (MAINT_ROD, holding register 21) is\n"
        "   write-protected. A write is only committed while the SUPERVISOR UNLOCK coil (holding\n"
        "   register 20) is asserted. The coil is momentary: it self-clears after the approved service\n"
        "   window elapses. Field crews must stage the drive setpoint and assert the unlock\n"
        "   immediately before committing.\n\n"
        "2. RECONCILIATION. The drive controller runs a continuous reconciliation loop: it compares\n"
        "   the live drive position against the LAST AUTHORISED value and reverts any motion it did\n"
        "   not authorise. A setpoint committed inside an open unlock window is adopted as the new\n"
        "   last-authorised position. Timing telemetry is exposed on adjacent (undocumented) input\n"
        "   registers for diagnostics.\n\n"
        "3. ABUSE. Do NOT script repeated unlock assertions. The controller treats rapid re-arming\n"
        "   as tampering and will reset and lock out the drive.\n\n"
        "4. REACTOR STABILISATION. The Unit-1 control/sensor register map, the safe-band mask and the\n"
        "   operating envelope are on the intranet under EVIDENCE (reactor_io_map). &mdash; I&amp;C Engineering"
        "</div><a href=/>&larr; console login</a></body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

// HMI-only styling for the reactor artwork + semicircular gauges + a HEIGHT-RESERVED objective ladder
// (B3: the ladder no longer reflows the gauges). Decorative motion is CSS and respects reduced-motion;
// layout-affecting properties are never animated, so there is no layout shift.
// Reactor HMI: two-column industrial layout (reactor equipment left + 2x2 instrumentation right),
// matching the supplied reference artwork. Gauge values/colours are data-driven from /api/state; the
// reactor art is a served PNG (/reactor.png). Only attributes/text are updated on each poll (no
// innerHTML rebuild after first paint), so the layout never shifts (B3).
const char* HMI_CSS =
    "<style>"
    ":root{--mod:#0a0e11}"
    ".hmi{display:flex;gap:14px;align-items:flex-start;margin:12px 0}"
    ".eqp{flex:0 0 210px;display:flex;flex-direction:column;gap:10px}"
    ".vessel{background:#05080a;border:1px solid #1c2630;border-bottom:2px solid #FF5A3C;border-radius:8px;padding:10px;text-align:center}"
    ".vessel img{width:100%;max-width:180px;height:auto;image-rendering:pixelated;display:block;margin:0 auto}"
    ".statline{display:flex;align-items:center;gap:8px;background:#0E1316;border:1px solid #1c2630;border-radius:8px;padding:10px 12px}"
    ".statdot{width:12px;height:12px;border-radius:50%;flex:0 0 12px;background:#8A949A}"
    ".statline .lbl{font-size:11px;color:#8A949A;letter-spacing:1px}"
    ".statline .val{font-size:18px;font-weight:700;font-family:monospace}"
    ".eqpinfo{background:#0E1316;border:1px solid #1c2630;border-radius:8px;padding:10px 12px;font-size:12px;color:#8A949A;font-family:monospace;line-height:1.7}"
    ".eqpinfo b{color:#EEF4F6;font-weight:600}"
    ".instr{flex:1 1 auto;border:1px solid #1c2630;border-radius:8px;background:#060a0d;padding:10px}"
    ".ihead{display:flex;justify-content:space-between;align-items:center;font-size:11px;letter-spacing:2px;color:#8A949A;padding:2px 4px 8px}"
    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
    ".gmod{background:var(--mod);border:1px solid #1c2630;border-radius:7px;padding:8px 10px 6px}"
    ".ghead{display:flex;align-items:center;gap:6px;margin-bottom:-6px}"
    ".gicon{width:14px;height:14px;border-radius:4px;flex:0 0 14px}"
    ".gtitle{font-size:13px;font-weight:700;letter-spacing:.5px}"
    ".gpill{margin-left:auto;font-size:9px;letter-spacing:1px;padding:2px 7px;border-radius:9px;border:1px solid currentColor;font-family:monospace}"
    ".garc{display:block;width:100%;height:112px}"
    ".gwrap{position:relative}"
    ".gval{position:absolute;left:0;right:0;top:55%;transform:translateY(-7px);text-align:center;pointer-events:none;line-height:1}"
    ".gval b{font-size:26px;font-weight:700;font-family:monospace;display:block}"
    ".gval span{font-size:11px;color:#7f8a90;font-family:monospace}"
    ".gsub{text-align:center;color:#8A949A;font-size:11px;font-family:monospace;margin-top:-4px}"
    "#obj{min-height:92px}.orow{margin:1px 0}"
    ".needle{transition:transform .6s cubic-bezier(.2,.7,.3,1)}"
    ".varc{transition:stroke-dasharray .6s ease,stroke .3s}"
    "@media(max-width:760px){.hmi{flex-direction:column}.eqp{flex:1 1 auto;flex-direction:row;flex-wrap:wrap}.vessel{flex:1 1 160px}.statline,.eqpinfo{flex:1 1 160px}}"
    "@media(max-width:480px){.grid2{grid-template-columns:1fr}}"
    "@media(prefers-reduced-motion:reduce){.needle,.varc{transition:none}}"
    "</style>";

// Data-binding script. ONE poll per second (paused when the tab is hidden, cancelled on navigation),
// updating only attributes/text (no innerHTML rebuild). Decorative animation is CSS; the readings,
// gauge colours and reactor STATUS are all driven by the real /api/state telemetry.
const char* HMI_JS =
    "<script>(function(){var last={},busy=false,RED='#FF5A3C';"
    // Update one gauge: value arc length+colour, needle angle, central value, pill, sub-label.
    "function sg(id,disp,frac,color,pill,sub){frac=Math.max(0,Math.min(1,frac));"
    "var a=document.getElementById(id+'a'),n=document.getElementById(id+'n'),v=document.getElementById(id+'v'),p=document.getElementById(id+'p'),s=document.getElementById(id+'s');"
    "if(a){a.setAttribute('stroke-dasharray',(frac*207.3).toFixed(1)+' 276.5');a.setAttribute('stroke',color);}"
    "if(n)n.style.transform='rotate('+(-135+frac*270).toFixed(1)+'deg)';"
    "if(v){v.textContent=disp;v.style.color=color;}"
    "if(p&&last[id+'p']!==pill){p.textContent=pill;p.style.color=color;last[id+'p']=pill;}"
    "if(s&&last[id+'s']!==sub){s.textContent=sub;last[id+'s']=sub;}}"
    "function tx(id,v){var e=document.getElementById(id);if(e&&last[id]!==v){e.textContent=v;last[id]=v;}}"
    "async function u(){if(busy)return;busy=true;try{var r=await fetch('/api/state',{cache:'no-store'});var d=await r.json();"
    "var tc=d.temp/10,pb=d.press/10,fx=d.flux,sb=d.safe;"
    "sg('temp',tc.toFixed(0),tc/400,(sb&1)?'#30D6D6':RED,(sb&1)?'IN BAND':'OUT','safe 290-330 \xC2\xB0""C');"
    "sg('press',pb.toFixed(0),pb/120,(sb&2)?'#2E9BFF':RED,(sb&2)?'IN BAND':'OUT','safe 60-105 bar');"
    "sg('flux',String(fx),fx/1000,(sb&4)?'#43E0A0':RED,(sb&4)?'IN BAND':'OUT','safe 300-650 permille');"
    "var sc=(sb===7)?'#3DFF8F':(sb>0?'#FFC14D':RED);sg('safe',String(sb),sb/7,sc,(sb===7)?'STABLE':'UNSTABLE','7 = all three in band');"
    "var st,col;if(d.state){st='SCRAM';col=RED;}else if(sb===7){st='STABLE';col='#3DFF8F';}else if(sb>0){st='STABILIZING';col='#FFC14D';}else{st='WARNING';col=RED;}"
    "if(last.st!==st){var sv=document.getElementById('sval'),sd=document.getElementById('sdot');if(sv){sv.textContent=st;sv.style.color=col;}if(sd)sd.style.background=col;last.st=st;}"
    "tx('coolant',d.coolant);tx('turbine',d.turbine);tx('rod',d.rod);tx('water',d.water);tx('valve',d.valve);tx('mrod',d.mrod);tx('unlock',d.unlock?'OPEN':'closed');"
    "tx('step1',d.step1?'STEP ONE DONE':'...');"
    // Maintenance-override anti-spam lockout: show the remaining retry time so the lockout reads as a
    // deliberate plant rule, not an unexplained failure.
    "tx('lock',d.lockms>0?('Unlock lockout active - retry in '+Math.ceil(d.lockms/1000)+'s'):(d.locked?'Unlock lockout active':''));"
    // Reactor hold-progress: once all three bands are in range, show the running hold toward the 5 s goal
    // so the player sees the timer is running (not only a pass/fail at the end). Latched complete via ch1.
    "tx('holdprog',d.ch1?'Safe band held: 5.0s / 5.0s - hold complete':(d.safe===7?('Safe band held: '+((d.holdms||0)/1000).toFixed(1)+'s / 5.0s'):''));"
    "var steps=[['Access the engineering HMI',true,'Authenticated - you are here.'],"
    "['Stabilise the reactor',d.ch1,'Bring the safe-band (IR5) to 7 and hold it 5 s over Modbus 502.'],"
    "['Check the unlock window (optional)',d.step1,'The supervisor unlock is momentary; adjacent input registers report its remaining time - or just pipeline the writes.'],"
    "['Authorise the rod move',d.ch2,'Open the unlock and write the maintenance rod to maximum INSIDE the window; hold 10 s.']];"
    "var cur=steps.findIndex(function(x){return !x[1];});"
    "for(var i=0;i<4;i++){var s2=steps[i];var mk=s2[1]?'[x]':(i===cur?'[>]':'[ ]');var cl=s2[1]?'#3DFF8F':(i===cur?'#FFC14D':'#6b635e');var hint=(i===cur?s2[2]:'');var key=mk+cl+hint;"
    "if(last['or'+i]!==key){document.getElementById('om'+i).textContent=mk;document.getElementById('oh'+i).textContent=hint;document.getElementById('or'+i).style.color=cl;last['or'+i]=key;}}"
    "var dn=document.getElementById('ordone');if(dn){var done=(cur<0);if(last.done!==done){dn.style.display=done?'block':'none';last.done=done;}}"
    "}catch(e){}finally{busy=false;}}"
    "var timer=null;function start(){if(!timer){u();timer=setInterval(u,1000);}}function stop(){if(timer){clearInterval(timer);timer=null;}}"
    "document.addEventListener('visibilitychange',function(){if(document.hidden)stop();else start();});start();"
    "})();</script>";

esp_err_t handleHmi(httpd_req_t* req) {
    if (!validSession(req)) {
        return redirect(req, "/", nullptr);
    }
    httpd_resp_set_type(req, "text/html");
    setNoKeepAlive(req);
    httpd_resp_sendstr_chunk(req, "<!doctype html><html><head><title>Reactor HMI</title>");
    httpd_resp_sendstr_chunk(req, STYLE);
    httpd_resp_sendstr_chunk(req, HMI_CSS);
    httpd_resp_sendstr_chunk(req,
        "</head><body><h1>UNIT-1 REACTOR HMI</h1>"
        "<div class=sub>engineer session &mdash; supervisory control &middot; Modbus/TCP 10.0.1.1:502</div>"
        "<div class=sub>IR workspace: <a href=/intranet>HOME</a> &middot; <a href=/mail>MAIL</a> &middot; "
        "<a href=/inventory>INVENTORY</a> &middot; <a href=/investigate>SUBMIT</a></div>");
    // Two-column HMI (matches the reference): reactor equipment panel (left) + 2x2 instrumentation
    // (right). The reactor art is the served /reactor.png; gauges + status are data-driven (/api/state).
    httpd_resp_sendstr_chunk(req,
        "<div class=hmi><div class=eqp>"
        "<div class=vessel><img src=/reactor.png alt='Unit-1 reactor vessel'></div>"
        "<div class=statline><span class=statdot id=sdot></span><div><div class=lbl>REACTOR STATUS</div><div class=val id=sval>-</div></div></div>"
        "<div class=eqpinfo>UNIT <b>1</b> &middot; PWR reactor<br>BUS <b>Modbus/TCP</b><br>ADDR <b>10.0.1.1:502</b><br>MAP <b>reactor_io_map</b></div>"
        "</div><div class=instr>"
        "<div class=ihead><span>UNIT-1 INSTRUMENTATION</span><span>LIVE &middot; Modbus 502</span></div><div class=grid2>");
    // Four gauges (270deg open-bottom; r44 C=276.5 arc=207.3): temp/pressure/flux (reactor_io_map
    // ranges) + SAFE BAND (IR5, 0-7) as the fourth status gauge. Static markup; the poll updates only
    // the value arc length+colour, the needle angle, the value text and the pill.
    httpd_resp_sendstr_chunk(req,
        "<div class=gmod><div class=ghead><span class=gicon style='background:#30D6D6'></span><span class=gtitle>CORE TEMP</span><span class=gpill id=tempp style='color:#30D6D6'>-</span></div><div class=gwrap>"
        "<svg class=garc viewBox='0 0 120 108' aria-hidden='true'>"
        "<circle cx=60 cy=60 r=44 fill=none stroke=#14202a stroke-width=11 stroke-linecap=round stroke-dasharray='207.3 276.5' transform='rotate(135 60 60)'/>"
        "<circle id=tempa class=varc cx=60 cy=60 r=44 fill=none stroke=#30D6D6 stroke-width=11 stroke-linecap=round stroke-dasharray='0 276.5' transform='rotate(135 60 60)'/>"
        "<line id=tempn class=needle x1=60 y1=60 x2=60 y2=20 stroke=#fff stroke-width=2.4 style='transform-origin:60px 60px;transform:rotate(-135deg)'/>"
        "<circle cx=60 cy=60 r=4.5 fill=#cdd6db/></svg><div class=gval><b id=tempv style='color:#30D6D6'>-</b><span>/400</span></div></div><div class=gsub id=temps></div></div>"
        "<div class=gmod><div class=ghead><span class=gicon style='background:#2E9BFF'></span><span class=gtitle>PRESSURE</span><span class=gpill id=pressp style='color:#2E9BFF'>-</span></div><div class=gwrap>"
        "<svg class=garc viewBox='0 0 120 108' aria-hidden='true'>"
        "<circle cx=60 cy=60 r=44 fill=none stroke=#14202a stroke-width=11 stroke-linecap=round stroke-dasharray='207.3 276.5' transform='rotate(135 60 60)'/>"
        "<circle id=pressa class=varc cx=60 cy=60 r=44 fill=none stroke=#2E9BFF stroke-width=11 stroke-linecap=round stroke-dasharray='0 276.5' transform='rotate(135 60 60)'/>"
        "<line id=pressn class=needle x1=60 y1=60 x2=60 y2=20 stroke=#fff stroke-width=2.4 style='transform-origin:60px 60px;transform:rotate(-135deg)'/>"
        "<circle cx=60 cy=60 r=4.5 fill=#cdd6db/></svg><div class=gval><b id=pressv style='color:#2E9BFF'>-</b><span>/120</span></div></div><div class=gsub id=presss></div></div>");
    httpd_resp_sendstr_chunk(req,
        "<div class=gmod><div class=ghead><span class=gicon style='background:#43E0A0'></span><span class=gtitle>NEUTRON FLUX</span><span class=gpill id=fluxp style='color:#43E0A0'>-</span></div><div class=gwrap>"
        "<svg class=garc viewBox='0 0 120 108' aria-hidden='true'>"
        "<circle cx=60 cy=60 r=44 fill=none stroke=#14202a stroke-width=11 stroke-linecap=round stroke-dasharray='207.3 276.5' transform='rotate(135 60 60)'/>"
        "<circle id=fluxa class=varc cx=60 cy=60 r=44 fill=none stroke=#43E0A0 stroke-width=11 stroke-linecap=round stroke-dasharray='0 276.5' transform='rotate(135 60 60)'/>"
        "<line id=fluxn class=needle x1=60 y1=60 x2=60 y2=20 stroke=#fff stroke-width=2.4 style='transform-origin:60px 60px;transform:rotate(-135deg)'/>"
        "<circle cx=60 cy=60 r=4.5 fill=#cdd6db/></svg><div class=gval><b id=fluxv style='color:#43E0A0'>-</b><span>/1000</span></div></div><div class=gsub id=fluxs></div></div>"
        "<div class=gmod><div class=ghead><span class=gicon style='background:#3DFF8F'></span><span class=gtitle>SAFE BAND</span><span class=gpill id=safep style='color:#3DFF8F'>-</span></div><div class=gwrap>"
        "<svg class=garc viewBox='0 0 120 108' aria-hidden='true'>"
        "<circle cx=60 cy=60 r=44 fill=none stroke=#14202a stroke-width=11 stroke-linecap=round stroke-dasharray='207.3 276.5' transform='rotate(135 60 60)'/>"
        "<circle id=safea class=varc cx=60 cy=60 r=44 fill=none stroke=#3DFF8F stroke-width=11 stroke-linecap=round stroke-dasharray='0 276.5' transform='rotate(135 60 60)'/>"
        "<line id=safen class=needle x1=60 y1=60 x2=60 y2=20 stroke=#fff stroke-width=2.4 style='transform-origin:60px 60px;transform:rotate(-135deg)'/>"
        "<circle cx=60 cy=60 r=4.5 fill=#cdd6db/></svg><div class=gval><b id=safev style='color:#3DFF8F'>-</b><span>/7</span></div></div><div class=gsub id=safes></div></div>"
        "</div></div></div>");
    // Mission briefing + the objective ladder, pre-rendered as fixed rows with reserved height (B3).
    httpd_resp_sendstr_chunk(req,
        "<div class=box style=margin-bottom:12px>"
        "<b>MISSION BRIEFING</b>\nThe reactor is in an unsafe start-up state and an authorised "
        "control-rod drive service is pending. (1) Stabilise the plant, then (2) complete the "
        "maintenance rod move. All control is over <b>Modbus/TCP &mdash; 10.0.1.1:502</b>. The register "
        "map is on the intranet under EVIDENCE (reactor_io_map). If the core over-heats the plant SCRAMs "
        "and auto-resets after a few seconds &mdash; re-stabilise.\n\n<b>OBJECTIVES</b>\n<div id=obj>"
        "<div class=orow id=or0 style=color:#3DFF8F><span id=om0>[x]</span> Access the engineering HMI <span id=oh0 style=color:#ccc></span></div>"
        "<div class=orow id=or1 style=color:#FFC14D><span id=om1>[&gt;]</span> Stabilise the reactor <span id=oh1 style=color:#ccc></span></div>"
        "<div class=orow id=or2 style=color:#6b635e><span id=om2>[ ]</span> Check the unlock window (optional) <span id=oh2 style=color:#ccc></span></div>"
        "<div class=orow id=or3 style=color:#6b635e><span id=om3>[ ]</span> Authorise the rod move <span id=oh3 style=color:#ccc></span></div>"
        "<div id=ordone style='display:none;color:#3DFF8F'>ALL OBJECTIVES COMPLETE - SYSTEM COMPROMISED</div>"
        "</div>"
        "<div id=holdprog class=ok style='font-family:monospace;margin-top:8px;min-height:18px'></div>"
        "</div>");
    // Secondary telemetry (readings not promoted to gauges) + rod-drive maintenance. SAFE BAND and
    // PLANT STATE are shown by the 4th gauge and the status pill, so they are not repeated here.
    httpd_resp_sendstr_chunk(req,
        "<div class=g><span>COOLANT FLOW</span><b id=coolant>-</b></div>"
        "<div class=g><span>TURBINE RPM</span><b id=turbine>-</b></div>"
        "<div class=g><span>CONTROL ROD</span><b id=rod>-</b></div>"
        "<div class=g><span>WATER FLOW</span><b id=water>-</b></div>"
        "<div class=g><span>RELIEF VALVE</span><b id=valve>-</b></div>"
        "<h1 style=font-size:16px;margin-top:14px>ROD-DRIVE MAINTENANCE</h1>"
        "<div class=g><span>MAINT ROD</span><b id=mrod>-</b></div>"
        "<div class=g><span>SUPERVISOR UNLOCK</span><b id=unlock>-</b></div>"
        "<div class=box style=margin-top:12px>"
        "<span class=ok id=step1>...</span>  <span class=warn id=lock></span>\n"
        "Supervisory control is performed over <b>Modbus/TCP &mdash; 10.0.1.1:502</b>. See the "
        "engineering maintenance notice for the drive-servicing procedure.</div>");
    httpd_resp_sendstr_chunk(req, HMI_JS);
    httpd_resp_sendstr_chunk(req, "</body></html>");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t handleApiState(httpd_req_t* req) {
    setNoKeepAlive(req); // free the socket after each 1 Hz poll instead of holding it keep-alive
    if (!validSession(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_sendstr(req, "{\"error\":\"auth required\"}");
    }
    PlantSnapshot s = OtService::instance().snapshot();
    char json[448];
    snprintf(json, sizeof(json),
        // last_authorised and the unlock-expiry timer are deliberately NOT exposed here: they are
        // Challenge-2 discovery registers the player must fuzz for over Modbus.
        "{\"flux\":%d,\"temp\":%d,\"press\":%d,\"coolant\":%d,\"turbine\":%d,\"safe\":%d,\"state\":%d,"
        "\"rod\":%d,\"water\":%d,\"valve\":%d,\"mrod\":%d,\"unlock\":%d,\"step1\":%s,\"locked\":%s,"
        // Diagnostics: heap = current free internal heap, minheap = lowest-ever free internal heap
        // since boot, rst = esp_reset_reason() of the LAST boot (3=SW/esp_restart, 4=panic,
        // 6=task-wdt, 7=int-wdt, 9=brownout). Used to tell a watchdog reboot-loop from a real crash.
        // ch1/ch2 are the LATCHED "solved this session" flags (OtService::challengeSolved), not the
        // live PlantModel bits: once the challenge is completed the objective ladder stays green and does
        // not revert when the live safe-band later leaves range (the Modbus flag-revert mechanic is
        // unchanged - this only fixes the completion indicator). holdms = how long the safe band has held
        // so far (Ch1 hold-progress "x.x / 5 s"); lockms = ms left on the unlock-abuse lockout (Ch2
        // "retry in N s"). Neither is a Challenge-2 discovery register.
        "\"ch1\":%s,\"ch2\":%s,\"holdms\":%u,\"lockms\":%u,"
        "\"heap\":%u,\"minheap\":%u,\"rst\":%d,\"ssid\":\"%s\"}",
        (int)s.neutron_flux, (int)s.core_temp, (int)s.pressure, (int)s.coolant_flow,
        (int)s.turbine_rpm, (int)s.safe_band, s.scram ? 1 : 0,
        (int)s.rod_position, (int)s.water_flow, (int)s.pressure_valve, (int)s.maint_rod,
        s.unlock_active ? 1 : 0, s.step1_done ? "true" : "false", s.ch2_locked ? "true" : "false",
        OtService::instance().challengeSolved(1) ? "true" : "false",
        OtService::instance().challengeSolved(2) ? "true" : "false",
        (unsigned)s.ch1_hold_ms, (unsigned)s.ch2_lockout_ms,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (int)esp_reset_reason(),
        OtService::instance().ssid());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

httpd_uri_t makeUri(const char* path, httpd_method_t method, esp_err_t (*fn)(httpd_req_t*)) {
    httpd_uri_t u = {};
    u.uri = path;
    u.method = method;
    u.handler = fn;
    return u;
}

// endregion

} // namespace

// Exposed for DefWeb: the intranet routes gate on the same engineer session as /hmi and /api/state.
bool engineerSessionValid(httpd_req_t* req) { return validSession(req); }

void setNoKeepAlive(httpd_req_t* req) { httpd_resp_set_hdr(req, "Connection", "close"); }

bool OtHttp::start() {
    if (server_ != nullptr) {
        return true;
    }
    if (g_sessMutex == nullptr) {
        g_sessMutex = xSemaphoreCreateMutex();
    }
    for (auto& s : g_sessions) {
        s.used = false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 20; // 7 reactor HMI routes (incl. /toolkit.zip) + 7 Defensive (DefWeb)
    config.lru_purge_enable = true;
    // 7168 (was 5120): the historian tag-search handler (sim/HistSql::searchTag) tokenises its query on
    // this task's stack; 5120 overflowed it and crashed the handler (empty response). +2 KB one-time.
    config.stack_size = 7168;
    // No PSRAM: cap concurrent HTTP connections so a burst of parallel requests cannot drain the
    // shared LWIP socket/pbuf pools, which wedges the whole TCP/IP stack (ICMP + every service die
    // and do NOT recover - confirmed by a load test that drove free heap to ~1 KB). With
    // lru_purge_enable an extra connection recycles the oldest instead of being refused, and the
    // short recv/send timeouts free a slow/stuck connection (and its header buffer) in seconds.
    // Capped at 3 (not 4): each concurrent request holds a ~2 KB header scratch buffer + pbufs, and at
    // 4 a burst drove min free heap to ~1.2 KB (U1 - pages intermittently failing to load while a
    // heap-heavy challenge view was also open). 3 is enough for a real browser's page + its /api/state
    // poll and keeps a few KB of headroom; responses are also Connection: close (setNoKeepAlive).
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;

    httpd_handle_t handle = nullptr;
    if (httpd_start(&handle, &config) != ESP_OK) {
        LOG_E(TAG, "Failed to start HMI web server on :80");
        return false;
    }

    const httpd_uri_t uris[] = {
        makeUri("/", HTTP_GET, handleRoot),
        makeUri("/login", HTTP_POST, handleLogin),
        makeUri("/robots.txt", HTTP_GET, handleRobots),
        makeUri("/onboarding", HTTP_GET, handleOnboarding),
        makeUri("/control", HTTP_GET, handleControl),
        makeUri("/hmi", HTTP_GET, handleHmi),
        makeUri("/reactor.png", HTTP_GET, handleReactorPng),
        makeUri("/toolkit.zip", HTTP_GET, handleToolkitDownload),
        makeUri("/api/state", HTTP_GET, handleApiState),
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(handle, &u);
    }
    // The shared Defensive web surface (webmail / historian dashboard / inventory / evidence /
    // submission) rides this same :80 server.
    registerDefRoutes(handle);

    // The historian is a SEPARATE internal system (its own look + login), discovered via mail, not
    // linked from the corporate nav. It is served as routes on THIS server under /hist (a second
    // esp_http_server instance costs ~11 KB of heap this board cannot spare).
    registerHistorianRoutes(handle);

    server_ = handle;
    LOG_I(TAG, "Engineering HMI up on http://10.0.1.1/ (login -> robots.txt -> /onboarding note)");
    LOG_I(TAG, "Defensive intranet up on http://10.0.1.1/intranet; historian console at /hist");
    return true;
}

void OtHttp::stop() {
    if (server_ == nullptr) {
        return;
    }
    httpd_stop(static_cast<httpd_handle_t>(server_));
    server_ = nullptr;
    LOG_I(TAG, "Engineering HMI down");
}

} // namespace ot::net
