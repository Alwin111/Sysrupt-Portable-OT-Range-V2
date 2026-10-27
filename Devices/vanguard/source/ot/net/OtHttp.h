// SPDX-License-Identifier: Apache-2.0
#pragma once

// The intentional web HMI front door for the OT range (port 80), matching the designed challenge
// entry: an operator login, a robots.txt that leaks the control path, and an admin control panel
// reachable by a broken function-level authorization check (the privilege escalation). This is the
// range's own HTTP server -- Tactility's filesystem web server stays disabled.
//
// It is a live *view* onto the reactor (named sensor/actuator gauges, polled as JSON) so a player
// can watch values move while they probe the Modbus register map; the actual control channel is
// still Modbus/TCP on 502.

struct httpd_req; // from esp_http_server.h (httpd_req_t)

namespace ot::net {

// True if the request carries a valid engineer session cookie (set at /login). Exposed so the
// Defensive intranet (DefWeb) can gate its routes behind the same engineer login as the reactor HMI:
// only /robots.txt and /control (which discloses the credentials) are reachable unauthenticated.
bool engineerSessionValid(httpd_req* req);

// Mark the response non-persistent (Connection: close) so the embedded httpd frees the socket + its
// request buffer as soon as the response is sent. On this heap-constrained device the socket pool is
// capped at 4; without this a browser's idle keep-alive connections tie those slots up (and ~2 KB of
// header buffer each), which starves new page loads under concurrency. Called by the page/API handlers
// here and in DefWeb.
void setNoKeepAlive(httpd_req* req);

// Shared SYSRUPT web design system - a clean, light industrial-software theme used by EVERY web page
// (OtHttp, DefWeb, HistWeb) so the whole HMI is one cohesive product. Defined once here (inline
// constexpr = a single definition across translation units). Warm off-white page, white surfaces with
// subtle shadows, dark graphite-teal header accent, charcoal text, one restrained teal interactive
// accent, semantic green/amber/red for real states. Body is the centred page container so existing
// per-page markup adopts the theme without structural edits.
inline constexpr const char* kWebStyle =
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>"
    // SYSRUPT palette: deep brown #703F37 (nav/headings/branding), dusty blue #7D94B5 (tabs/interactive/
    // instrumentation; --acc is a darker variant for accessible link/active text), sage #B6C199 (normal
    // flow; --grn is a darker sage for readable healthy-state text). Warm light-gray ground + neutrals.
    ":root{--bg:#f3f1ec;--surf:#ffffff;--surf2:#f1efe8;--nav:#703f37;--navtx:#f3ece8;--navmut:#cbb3ac;"
    "--txt:#2a2623;--mut:#776f68;--line:#dadecd;--acc:#4c6485;--acc2:#3b4f6b;--blue:#7d94b5;"
    "--brown:#703f37;--brown2:#5a322c;--sage:#b6c199;--grn:#5f6e45;--amb:#b7791f;--red:#b23b2e;"
    "--mono:ui-monospace,'SFMono-Regular',Menlo,Consolas,monospace;"
    "--sans:'Segoe UI',Roboto,-apple-system,BlinkMacSystemFont,system-ui,sans-serif}"
    "*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}"
    "body{background:var(--bg);color:var(--txt);font-family:var(--sans);font-size:15px;line-height:1.5;"
    "margin:0 auto;padding:22px 24px 40px;max-width:1120px}"
    "h1{color:var(--brown);font-size:22px;font-weight:700;letter-spacing:.2px;margin:0 0 4px}"
    "h2{color:var(--brown);font-size:18px;font-weight:600;margin:20px 0 12px}"
    "h3{color:var(--mut);font-size:12px;font-weight:700;letter-spacing:.6px;text-transform:uppercase;margin:18px 0 10px}"
    ".sub{color:var(--mut);margin:0 0 18px;font-size:13px}.sub a{margin-right:4px}"
    "a{color:var(--acc);text-decoration:none}a:hover{color:var(--acc2);text-decoration:underline}"
    "code,pre,.mono,td,.g b,.stat-card b{font-family:var(--mono)}"
    "hr{border:0;border-top:1px solid var(--line);margin:6px 0 16px}"
    // Full-bleed deep-brown header: spans the whole viewport width via negative margins while its inner
    // row stays aligned to the 1120px content column. margin-top cancels the body's top padding so there
    // is no gap above it.
    ".topbar{background:linear-gradient(180deg,#7d4a40,#683a33);color:var(--navtx);"
    "border-bottom:3px solid var(--blue);box-shadow:0 2px 6px rgba(20,12,10,.15);"
    "width:100vw;margin-left:calc(50% - 50vw);margin-top:-22px;margin-bottom:22px}"
    ".topbar-inner{max-width:1120px;margin:0 auto;padding:16px 24px;display:flex;align-items:center;"
    "justify-content:space-between;flex-wrap:wrap;gap:6px 16px}"
    ".brand{font-weight:700;font-size:15px;letter-spacing:.5px;color:#fff}"
    ".brand i{color:var(--sage);font-style:normal;font-weight:600;font-size:12px;margin-left:10px;letter-spacing:.3px}"
    ".topbar nav a{color:var(--navmut);margin-left:18px;font-size:13px}.topbar nav a:hover{color:#fff}"
    ".topbar nav a.active{color:#fff;border-bottom:2px solid var(--blue);padding-bottom:3px}"
    "nav{display:flex;flex-wrap:wrap;gap:4px 18px;margin-bottom:14px}"
    "nav a{font-size:14px;color:var(--mut);padding:2px 0}nav a:hover{color:var(--txt)}"
    "nav a.active{color:var(--acc);font-weight:700;border-bottom:2px solid var(--acc)}"
    ".box,.panel,.query-box,.card{background:var(--surf);border:1px solid var(--line);border-radius:8px;"
    "padding:16px 18px;box-shadow:0 1px 3px rgba(20,30,25,.06);margin-bottom:16px}"
    ".box{white-space:pre-wrap;font-size:14px;line-height:1.5}"
    ".stats{display:flex;gap:16px;margin-bottom:18px;flex-wrap:wrap}"
    ".stat-card{flex:1 1 150px;background:var(--surf);border:1px solid var(--line);border-radius:8px;"
    "box-shadow:0 1px 3px rgba(20,30,25,.06);padding:16px 20px;text-align:center}"
    ".stat-card b,.stat-card h3{display:block;font-size:26px;color:var(--txt);font-weight:700;margin:0;letter-spacing:0;text-transform:none}"
    ".stat-card span,.stat-card p{color:var(--mut);font-size:11px;margin:5px 0 0;letter-spacing:.6px;text-transform:uppercase}"
    ".g{display:inline-block;background:var(--surf);border:1px solid var(--line);border-radius:8px;"
    "box-shadow:0 1px 3px rgba(20,30,25,.06);padding:11px 16px;margin:5px;min-width:148px;vertical-align:top}"
    ".g b{color:var(--txt);display:block;font-size:22px;font-weight:700;margin-top:3px}"
    ".g span{color:var(--mut);font-size:11px;letter-spacing:.6px;text-transform:uppercase}"
    ".table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:8px;box-shadow:0 1px 3px rgba(20,30,25,.06);margin-bottom:16px}"
    "table{width:100%;border-collapse:collapse;background:var(--surf);font-size:13.5px}"
    "th,td{padding:9px 14px;text-align:left;border-bottom:1px solid var(--line)}"
    "th{background:var(--surf2);color:var(--mut);font-weight:600;font-size:11.5px;letter-spacing:.5px;text-transform:uppercase;white-space:nowrap}"
    "td{color:var(--txt);font-family:var(--mono);font-size:13px}"
    "tbody tr:hover{background:var(--surf2)}tbody tr:last-child td{border-bottom:0}"
    "label{display:block;margin:12px 0 5px;font-weight:600;color:var(--mut);font-size:13px}"
    "input,select,textarea{background:var(--surf);border:1px solid #cbd2cd;color:var(--txt);font-size:14px;"
    "padding:9px 11px;border-radius:5px;width:340px;max-width:100%;font-family:var(--sans)}"
    "textarea{width:100%;min-height:64px;resize:vertical;font-family:var(--mono)}"
    "input:focus,select:focus,textarea:focus{outline:none;border-color:var(--blue);box-shadow:0 0 0 3px rgba(125,148,181,.3)}"
    "button,.btn{background:var(--brown);color:#fff;border:0;padding:9px 20px;font-family:var(--sans);font-size:14px;"
    "font-weight:600;border-radius:5px;cursor:pointer;margin-top:12px}button:hover,.btn:hover{background:var(--brown2)}"
    ".btn-sec{background:var(--blue);color:#20303f;border:0}.btn-sec:hover{filter:brightness(.95)}"
    "a:focus-visible,button:focus-visible,input:focus-visible{outline:2px solid var(--acc);outline-offset:2px}"
    ".warn{color:var(--amb)}.ok{color:var(--grn)}.alarm{color:var(--red)}"
    "#st{min-height:18px;margin:6px 0;font-size:13px}"
    ".pill{display:inline-block;font-size:11px;font-weight:600;padding:2px 9px;border-radius:10px;letter-spacing:.4px}"
    ".pill.ok{background:#e9eedb;color:var(--grn)}.pill.warn{background:#fbf0d9;color:var(--amb)}.pill.alarm{background:#f6e3e0;color:var(--red)}"
    ".error{background:#fce9e6;color:#8c2f26;border:1px solid #f1c7c1;padding:10px 12px;border-radius:6px;margin:12px 0}"
    ".hint{color:var(--mut);font-size:13px;margin-top:8px}"
    ".login{min-height:84vh;display:flex;flex-direction:column;align-items:center;justify-content:center}"
    ".login h1{font-size:24px;text-align:center}.login .box,.login .card,.login-box{max-width:400px;width:100%;text-align:left}"
    ".foot,footer{text-align:center;color:var(--mut);font-size:12px;padding:18px;margin-top:28px;border-top:1px solid var(--line)}"
    // --- realistic UI components (home dashboard, webmail, inventory, submit) - additive, so the
    // existing .box/pre-wrap surfaces are untouched ---
    // Page header: title block + right-aligned meta, divided by a hairline.
    ".phead{display:flex;align-items:flex-end;justify-content:space-between;gap:10px 16px;flex-wrap:wrap;"
    "border-bottom:1px solid var(--line);padding-bottom:14px;margin-bottom:20px}"
    ".phead h1{margin:0}.phead .sub{margin:4px 0 0}"
    ".phead .meta{color:var(--mut);font-size:11.5px;font-family:var(--mono);text-align:right;line-height:1.55}"
    ".shead{display:flex;align-items:baseline;gap:10px;margin:24px 0 11px;flex-wrap:wrap}.shead h2{margin:0}.shead .c{color:var(--mut);font-size:12px}"
    // Info/incident card: left accent bar + round icon tile + real paragraphs (no pre-wrap).
    ".icard{display:flex;gap:14px;align-items:flex-start;background:var(--surf);border:1px solid var(--line);"
    "border-left:4px solid var(--blue);border-radius:10px;padding:15px 18px;box-shadow:0 1px 3px rgba(20,30,25,.07);margin-bottom:16px}"
    ".icard.alert{border-left-color:var(--amb)}.icard.good{border-left-color:var(--grn)}.icard.bad{border-left-color:var(--red)}"
    ".icard .ico{flex:0 0 38px;width:38px;height:38px;border-radius:9px;display:flex;align-items:center;"
    "justify-content:center;font-size:18px;background:var(--surf2);color:var(--brown)}"
    ".icard.alert .ico{color:var(--amb)}.icard.good .ico{color:var(--grn)}.icard.bad .ico{color:var(--red)}"
    ".icard .bd{flex:1;min-width:0}.icard .ttl{font-weight:700;font-size:13px;letter-spacing:.5px;color:var(--txt);margin-bottom:7px}"
    ".icard p{margin:0 0 9px;line-height:1.55}.icard p:last-child{margin-bottom:0}"
    // Webmail inbox: card with a header strip and avatar rows.
    ".mailbox{background:var(--surf);border:1px solid var(--line);border-radius:10px;box-shadow:0 1px 3px rgba(20,30,25,.07);overflow:hidden}"
    ".mbhead{display:flex;align-items:center;justify-content:space-between;padding:12px 18px;background:var(--surf2);border-bottom:1px solid var(--line)}"
    ".mbtitle{font-weight:700;color:var(--brown);font-size:14px;letter-spacing:.3px}.mbcount{color:var(--mut);font-size:12px}"
    ".mlist{list-style:none;margin:0;padding:0}.mrow{border-bottom:1px solid var(--line)}.mrow:last-child{border-bottom:0}"
    ".mlink{display:flex;align-items:center;gap:12px;padding:12px 18px;text-decoration:none;color:var(--txt)}"
    ".mlink:hover{background:var(--surf2);text-decoration:none}"
    ".mdot{flex:0 0 auto;width:8px;height:8px;border-radius:50%;background:var(--blue)}"
    ".avatar{flex:0 0 auto;width:36px;height:36px;border-radius:50%;background:var(--c,#7d94b5);color:#fff;"
    "display:flex;align-items:center;justify-content:center;font-weight:700;font-size:13px;letter-spacing:.3px}"
    ".mmid{flex:1;min-width:0}.mtop{display:flex;justify-content:space-between;gap:10px;align-items:baseline}"
    ".mfrom{font-weight:700;color:var(--txt);font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
    ".mdate{color:var(--mut);font-size:11.5px;font-family:var(--mono);white-space:nowrap;flex:0 0 auto}"
    ".mbot{display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:2px}"
    ".msub{color:var(--txt);font-size:13.5px}.msnip{color:var(--mut);font-size:13px}"
    ".mclip{color:var(--mut);font-size:14px;flex:0 0 auto}"
    ".mbadge{flex:0 0 auto;background:var(--surf2);border:1px solid var(--line);color:var(--mut);font-size:11px;padding:1px 7px;border-radius:9px;font-family:var(--mono)}"
    // Thread view: back link + subject, then one card per message with an avatar header.
    ".thtop{display:flex;align-items:center;gap:14px;margin-bottom:16px;flex-wrap:wrap}"
    ".thtop h2{margin:0;flex:1;min-width:180px}.thtop .thn{color:var(--mut);font-size:12px}"
    ".msg{background:var(--surf);border:1px solid var(--line);border-radius:10px;box-shadow:0 1px 3px rgba(20,30,25,.07);padding:16px 18px;margin-bottom:14px}"
    ".mhd{display:flex;align-items:center;gap:12px;padding-bottom:12px;border-bottom:1px solid var(--line);margin-bottom:12px}"
    ".mwho{flex:1;min-width:0;display:flex;flex-direction:column}.mname{font-weight:700;color:var(--txt);font-size:13.5px}.mto{color:var(--mut);font-size:11.5px}"
    ".mbody{white-space:pre-wrap;line-height:1.6;font-size:14px;margin:0}"
    // File attachment card: a type chip + filename + a download affordance (links to a real download).
    ".files{margin-top:14px;display:flex;flex-wrap:wrap;gap:10px}"
    ".file{display:flex;align-items:center;gap:12px;min-width:244px;max-width:340px;background:var(--surf2);"
    "border:1px solid var(--line);border-radius:8px;padding:9px 12px;text-decoration:none;color:var(--txt)}"
    ".file:hover{border-color:var(--blue);background:#eef1f6;text-decoration:none}"
    ".fx{flex:0 0 auto;width:34px;height:40px;border-radius:5px;background:var(--brown);color:#fff;display:flex;"
    "align-items:center;justify-content:center;font-size:10px;font-weight:700;font-family:var(--mono);letter-spacing:.4px}"
    ".fx.log{background:var(--acc)}.fx.csv{background:var(--grn)}"
    ".fmeta{flex:1;min-width:0;display:flex;flex-direction:column}"
    ".fname{font-weight:600;font-size:13px;color:var(--txt);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-family:var(--mono)}"
    ".fsub{color:var(--mut);font-size:11px;margin-top:2px}.fdl{flex:0 0 auto;color:var(--acc);font-size:17px}"
    // Submit: one card per challenge with a number badge and an inline form row.
    ".qcard{background:var(--surf);border:1px solid var(--line);border-radius:10px;box-shadow:0 1px 3px rgba(20,30,25,.07);padding:16px 18px;margin-bottom:16px}"
    ".qh{display:flex;align-items:center;gap:11px;margin-bottom:8px}"
    ".qnum{flex:0 0 auto;background:var(--brown);color:#fff;font-size:11px;font-weight:700;font-family:var(--mono);padding:3px 9px;border-radius:7px;letter-spacing:.5px}"
    ".qt{font-weight:700;font-size:14px;color:var(--txt);letter-spacing:.3px}.qcard p{margin:0 0 11px;line-height:1.55}"
    ".qform{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:0}"
    ".qform .flbl{font-size:12px;color:var(--mut);font-family:var(--mono)}"
    ".qform input{width:auto;flex:0 1 auto;min-width:130px}.qform input[name=flag]{flex:1 1 220px;max-width:360px}.qform button{margin-top:0}"
    "@media(max-width:600px){body{padding:16px 16px 32px}h1{font-size:19px}.g{min-width:44%}"
    ".phead .meta,.msg .mdate{text-align:left}.file{min-width:100%;max-width:100%}.msnip{display:none}}"
    "</style>";

class OtHttp {
public:
    bool start();
    void stop();
    bool running() const { return server_ != nullptr; }

private:
    void* server_ = nullptr; // httpd_handle_t
};

} // namespace ot::net
