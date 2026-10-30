// SPDX-License-Identifier: Apache-2.0
#include "ChallengeView.h"
#include "../Buzzer.h"
#include "Challenges.h"
#include "CompleteLayout.h"
#include "Instruments.h"
#include "RefUi.h"
#include "SystemTheme.h"
#include "Theme.h"

#include "../ot/Iec104Service.h"
#include "../ot/OpcUaService.h"
#include "../ot/OtService.h"
#include "../ot/PumpService.h"
#include "../ot/SisService.h"
#include "../ot/sim/PlantModel.h"
#include "assets/IconsImg.h"
#include "../ot/DamService.h"
#include "../ot/DefService.h"
#include "../ot/GooseService.h"
#include "../ot/SnmpService.h"

#include <app/event.h>
#include <app/scheduler.h>
#include <app/signal.h>
#include <app/start.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <csignal>
#include <cstdio>
#include <cstring>

// Reference: offensive-overview.png. Breadcrumb + difficulty/points, a tab bar (OVERVIEW active;
// HINTS/STATUS/OPERATIONS are Phase-2 placeholders), a left info panel (STORY / OBJECTIVES / ACCESS
// INFO) and a right LIVE TELEMETRY panel driven by the real challenge services (reusable gauges/tiles).
namespace ot::ui {

namespace {

namespace th = vanguard::theme;
namespace in = ot::ui::instr;
namespace rf = ot::ui::ref;

// Launch another app (by manifest id) on top of this one.
void startApp(const char* id) {
    AppStartContext ctx; AppInstanceId inst = 0;
    if (app_start_context_from_id(id, &ctx) == ERROR_NONE) {
        app_start_with_context(&ctx, &inst);
    }
}
// Clean self-close (SIGTERM -> APP_EVENT_CLOSE -> the event loop tears the window down properly).
void closeSelf() { app_signal_send(app_scheduler_current_app_id(), SIGTERM); }

constexpr uint32_t REFRESH_MS = 500;
constexpr int TAB_COUNT = 4;
constexpr const char* TAB_NAMES[TAB_COUNT] = { "OVERVIEW", "SYSTEM", "HINTS", "STATUS" };
constexpr int TAB_HINTS = 2; // HINTS is the third tab in the new order

constexpr int HINT_MAX = 4;

struct View {
    int number = 0;
    const ChallengeDef* def = nullptr;
    ChKind kind = ChKind::NotImplemented;
    bool hasGauge = false;
    in::Gauge gauge;
    in::Tile tiles[4];
    // Reactor1 ("Stabilise the Reactor") shows its three continuous process values as semicircular
    // gauges instead of the flat table - the three safe-band dials the player is driving. Other
    // challenges keep the compact PARAMETER/VALUE/SAFE table (categorical state reads better there).
    in::SemiGauge rGauge[3];
    bool useGauges = false;
    lv_obj_t* tabBtns[TAB_COUNT] = {};
    lv_obj_t* tabLbls[TAB_COUNT] = {};
    lv_obj_t* tabPanels[TAB_COUNT] = {};
    lv_obj_t* content = nullptr; // parent of the tab panels; kept so heavy panels can build lazily
    int activeTab = 0;
    lv_color_t accent = vanguard::theme::offensive();
    lv_group_t* grp = nullptr;
    uint32_t startTick = 0;
    // HINTS tab
    lv_obj_t* hintRows[HINT_MAX] = {};
    lv_obj_t* hintLocks[HINT_MAX] = {};
    lv_obj_t* hdTitle = nullptr;
    lv_obj_t* hdCost = nullptr;
    lv_obj_t* hdBody = nullptr;
    lv_obj_t* cfBox = nullptr;
    int hintSel = 0;
    bool confirming = false;
    bool hintsInGroup = false;
    bool rebuilding = false;
    // STATUS tab (live)
    lv_obj_t* stStatus = nullptr;
    lv_obj_t* stElapsed = nullptr;
    lv_obj_t* stProgress = nullptr;
    lv_obj_t* stBars[HINT_MAX] = {};
    lv_obj_t* stObjChecks[HINT_MAX] = {};
    lv_obj_t* stEarned = nullptr;
    lv_obj_t* stDeducted = nullptr;
    lv_obj_t* stNet = nullptr;
    lv_timer_t* timer = nullptr;
    lv_obj_t* root = nullptr; // window content root; reused in-place for the completion screen
    // Completion trigger: record whether the challenge was ALREADY complete when the view opened, so
    // the Challenge Complete screen fires only on a genuine not-completed -> completed edge while the
    // player is watching (and not when they re-open an already-solved challenge).
    bool wasCompleted = false;
    bool completeTriggered = false;
};

const char* assetName(ChKind k) {
    switch (k) {
        case ChKind::Reactor1:
        case ChKind::Reactor2: return "REACTOR";
        case ChKind::Iec104:   return "SUBSTN";
        case ChKind::Sis:      return "SIS";
        case ChKind::Pump:     return "PUMP";
        case ChKind::Opcua:    return "OPC-UA";
        case ChKind::Dam:      return "DAM GATE";
        case ChKind::Goose:    return "RELAY";
        case ChKind::Forensics:return "HISTRN";
        case ChKind::Trojan:   return "HISTRN";
        case ChKind::Device:   return "PLANT NET";
        case ChKind::SnmpRecovery: return "FIELD RTU";
        default:               return "TARGET";
    }
}

void refreshStatus(View* v); // defined with the STATUS-tab builders below

// --- live telemetry -> instruments (reused from the industrial-HMI design) -----------------------
void refreshReactor1(View* v) {
    const auto s = OtService::instance().snapshot();
    const bool scram = s.scram;
    if (v->useGauges) {
        // Gauge fill is % of a plausible operating span; colour is the real safe-band bit (green in
        // band, red out). core_temp is tenths-C (0-4000 -> 0-100%), pressure tenths-bar (0-1500),
        // neutron flux permille (0-1000).
        char g[16];
        int tp = s.core_temp / 40; if (tp > 100) tp = 100; if (tp < 0) tp = 0;
        snprintf(g, sizeof(g), "%d", s.core_temp / 10);
        in::semiGaugeSet(v->rGauge[0], tp, g, (s.safe_band & 0x1) ? th::green() : th::red());
        // Pressure full-scale 120 bar (pressure is tenths-bar), integer value: matches the web HMI
        // gauge (/api/state -> "/120", pb.toFixed(0)) so the dial and number read the same on both.
        int pp = s.pressure / 12; if (pp > 100) pp = 100; if (pp < 0) pp = 0;
        snprintf(g, sizeof(g), "%u", s.pressure / 10);
        in::semiGaugeSet(v->rGauge[1], pp, g, (s.safe_band & 0x2) ? th::green() : th::red());
        int fp = s.neutron_flux / 10; if (fp > 100) fp = 100; if (fp < 0) fp = 0;
        snprintf(g, sizeof(g), "%d", s.neutron_flux);
        in::semiGaugeSet(v->rGauge[2], fp, g, (s.safe_band & 0x4) ? th::green() : th::red());
        return;
    }
    char t[20];
    snprintf(t, sizeof(t), "%d.%d \xC2\xB0""C", s.core_temp / 10, (s.core_temp % 10 + 10) % 10);
    in::tileSet(v->tiles[0], t, (s.safe_band & 0x1) ? th::green() : th::red());
    snprintf(t, sizeof(t), "%u.%u bar", s.pressure / 10, s.pressure % 10);
    in::tileSet(v->tiles[1], t, (s.safe_band & 0x2) ? th::green() : th::red());
    snprintf(t, sizeof(t), "%d n/s", s.neutron_flux);
    in::tileSet(v->tiles[2], t, (s.safe_band & 0x4) ? th::green() : th::red());
    const bool stable = (s.safe_band == 0x7) && !scram;
    in::tileSet(v->tiles[3], stable ? "STABLE" : "UNSTABLE", stable ? th::green() : th::red());
}
void refreshReactor2(View* v) {
    auto& svc = OtService::instance();
    const auto s = svc.snapshot();
    char t[16]; snprintf(t, sizeof(t), "%d", s.maint_rod);
    in::tileSet(v->tiles[0], t, s.ch2_locked ? th::red() : (s.maint_rod >= 100 ? th::green() : th::accent()));
    in::tileSet(v->tiles[1], s.unlock_active ? "OPEN" : "closed", s.unlock_active ? th::green() : th::textMuted());
    in::tileSet(v->tiles[2], s.ch2_locked ? "LOCKOUT" : (svc.challengeSolved(2) ? "SOLVED" : "ready"),
                s.ch2_locked ? th::red() : (svc.challengeSolved(2) ? th::green() : th::textMuted()));
}
void refreshPump(View* v) {
    auto& p = PumpService::instance();
    const bool open = p.targetOpen();
    in::tileSet(v->tiles[0], open ? "OPEN" : "SHUT", p.alerted() ? th::red() : (open ? th::green() : th::textMuted()));
    in::tileSet(v->tiles[1], p.alerted() ? "TRIPPED" : "clear", p.alerted() ? th::red() : th::green());
    in::tileSet(v->tiles[2], p.solved() ? "SOLVED" : "running", p.solved() ? th::green() : th::textMuted());
}
void refreshIec104(View* v) {
    auto& i = Iec104Service::instance();
    in::tileSet(v->tiles[0], i.breakerOpen() ? "TRIPPED" : "closed", i.breakerOpen() ? th::warning() : th::green());
    in::tileSet(v->tiles[1], i.interrogationLock() ? "LOCKED" : "idle", i.interrogationLock() ? th::warning() : th::textMuted());
    in::tileSet(v->tiles[2], i.solved() ? "SOLVED" : "running", i.solved() ? th::green() : th::textMuted());
}
void refreshSis(View* v) {
    auto& s = SisService::instance();
    in::tileSet(v->tiles[0], s.tripEnabled() ? "ARMED" : "DISABLED", s.tripEnabled() ? th::green() : th::red());
    in::tileSet(v->tiles[1], s.checksumMatch() ? "valid" : "mismatch", s.checksumMatch() ? th::green() : th::warning());
    in::tileSet(v->tiles[2], s.solved() ? "SOLVED" : "running", s.solved() ? th::green() : th::textMuted());
}
void refreshDam(View* v) {
    auto& dm = DamService::instance();
    char g[12]; snprintf(g, sizeof(g), "%u", (unsigned)dm.gate());
    in::tileSet(v->tiles[0], g, dm.atTarget() ? th::green() : th::text());
    char t[12]; snprintf(t, sizeof(t), "%u", (unsigned)ot::dam::TARGET_GATE);
    in::tileSet(v->tiles[1], t, th::textMuted());
    in::tileSet(v->tiles[2], dm.solved() ? "SOLVED" : (dm.touched() ? "dialled" : "idle"),
                dm.solved() ? th::green() : th::textMuted());
}
void refreshOpcua(View* v) {
    auto& o = OpcUaService::instance();
    in::tileSet(v->tiles[0], o.touched() ? "active" : "idle", o.touched() ? th::accent() : th::textMuted());
    in::tileSet(v->tiles[1], o.solved() ? "found" : "hidden", o.solved() ? th::green() : th::warning());
    in::tileSet(v->tiles[2], o.solved() ? "SOLVED" : "running", o.solved() ? th::green() : th::textMuted());
}
void refreshGoose(View* v) {
    auto& g = GooseService::instance();
    in::tileSet(v->tiles[0], g.breakerOpen() ? "OPEN" : "closed", g.breakerOpen() ? th::red() : th::green());
    char st[12]; snprintf(st, sizeof(st), "%u", (unsigned)g.lastStNum());
    in::tileSet(v->tiles[1], st, th::textMuted());
    in::tileSet(v->tiles[2], g.solved() ? "SOLVED" : (g.touched() ? "probed" : "idle"),
                g.solved() ? th::green() : th::textMuted());
}
void refreshForensics(View* v) {
    // The badge only knows whether the extracted flag has been submitted - it has no visibility into
    // whether the player found the console or ran the injection. Do NOT surface the console path here:
    // its location is what the paid GET STARTED hint sells, and objective 1 is to discover it.
    auto& d = DefService::instance();
    bool solved = d.running() && d.forensics().solved();
    in::tileSet(v->tiles[0], "SQLi", th::textMuted());
    in::tileSet(v->tiles[1], solved ? "extracted" : "hidden", solved ? th::green() : th::warning());
    in::tileSet(v->tiles[2], solved ? "SOLVED" : "open", solved ? th::green() : th::textMuted());
}
void refreshTrojan(View* v) {
    auto& t = DefService::instance().trojan();
    in::tileSet(v->tiles[0], t.restored() ? "restored" : "gap", t.restored() ? th::green() : th::warning());
    in::tileSet(v->tiles[1], t.solved() ? "stable" : "holding", t.solved() ? th::green() : th::textMuted());
    in::tileSet(v->tiles[2], t.solved() ? "SOLVED" : "open", t.solved() ? th::green() : th::textMuted());
}
void refreshDevice(View* v) {
    auto& k = DefService::instance().takeover();
    in::tileSet(v->tiles[0], "4 stations", th::textMuted());
    in::tileSet(v->tiles[1], k.solved() ? "unmasked" : "spoofed", k.solved() ? th::green() : th::warning());
    in::tileSet(v->tiles[2], k.solved() ? "SOLVED" : "open", k.solved() ? th::green() : th::textMuted());
}
// #08 Field Device Recovery (SNMP). Shows the real device state from the live SNMP service so the
// SYSTEM tab is not blank: running image (last-good vs tampered), the health check, and whether the
// recovery has been verified. No answer/flag is revealed - only the device's own reported state.
void refreshSnmp(View* v) {
    auto& s = SnmpService::instance();
    const bool lastGood = s.system().runningImage() == ot::snmp::LAST_GOOD_IMAGE;
    in::tileSet(v->tiles[0], lastGood ? "last-good" : "TAMPERED", lastGood ? th::green() : th::warning());
    in::tileSet(v->tiles[1], s.healthy() ? "HEALTHY" : "unhealthy", s.healthy() ? th::green() : th::red());
    in::tileSet(v->tiles[2], s.solved() ? "RECOVERED" : (s.touched() ? "probing" : "open"),
                s.solved() ? th::green() : th::textMuted());
}

// Completion-screen button handlers (the completion screen is rendered in-place in this app).
void onCompleteNext(lv_event_t* e) {
    const int num = (int)(intptr_t)lv_event_get_user_data(e);
    const int n = nextChallengeNumber(num);
    if (n > 0) {
        setSelectedChallenge(n);
        startApp("vanguard.challenge"); // the next challenge opens on top (memory is free by now)...
    }
    closeSelf(); // ...and this view closes either way (no next -> falls back to the challenge list)
}
void onCompleteList(lv_event_t*) { closeSelf(); }

// Swap this challenge view's content for the Challenge Complete screen, in place. Rendering the
// completion screen as a SEPARATE app failed under memory pressure (the challenge view + services
// pin ~334 KB, leaving ~1.5 KB free, far too little for a second app's task + widgets). Doing it
// in-place frees the heavy challenge widgets first, then builds the lighter completion screen into
// the same window/task. Runs via lv_async_call, i.e. AFTER the triggering refresh() returns.
void doCompletionSwap(void* arg) {
    auto* v = static_cast<View*>(arg);
    lv_obj_t* root = v->root;
    const int num = v->number;
    sessionMarkComplete(num);               // persist the completion so it survives a reboot
    lv_obj_clean(root);                     // free all challenge widgets (big heap recovery)
    lv_obj_set_style_pad_all(root, 0, 0);   // plain full-bleed container for the completion screen
    const ChallengeDef& def = challengeByNumber(num);
    const bool offensive = def.category == Category::Offensive;
    const bool hasNext = nextChallengeNumber(num) > 0;
    CompleteButtons btns = buildCompleteScreen(root, offensive, displayTitle(def),
                                               difficultyLabel(def.difficulty), def.points,
                                               netScore(), SCORE_MAX, hasNext);
    lv_obj_add_event_cb(btns.next, onCompleteNext, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)num);
    lv_obj_add_event_cb(btns.list, onCompleteList, LV_EVENT_SHORT_CLICKED, nullptr);
    // Sound the success chime as the "challenge solved" screen appears (mirrors the inctf Vanguard
    // flag-accepted jingle). This is the real completion screen for every challenge; non-blocking.
    vanguard::buzzer::playSuccess();
    auto* grp = lv_group_get_default();
    if (grp) {
        lv_obj_update_layout(root);
        lv_group_focus_obj(btns.next);
        lv_obj_add_state(btns.next, LV_STATE_FOCUS_KEY);
    }
}

void refresh(lv_timer_t* timer) {
    auto* v = static_cast<View*>(lv_timer_get_user_data(timer));
    refreshStatus(v); // STATUS tab is live for every challenge (including defensive, which has no tiles)

    // Genuine-completion edge: if the challenge was not complete when this view opened and has just
    // become complete, swap this view for the Challenge Complete screen in-place (once). Pause the
    // refresh timer first so it stops touching the challenge widgets we are about to delete.
    // The system-level completion watcher (controls_task) detects a solve from ANY screen and posts
    // the challenge number; when it is THIS view's challenge, swap to the shared Challenge Complete
    // screen (once). This works whether the player was already on this view or the watcher opened it
    // for them, so a completion is never missed just because another screen was up.
    if (!v->completeTriggered && pendingCompletion() == v->number) {
        v->completeTriggered = true;
        clearPendingCompletion(v->number);
        lv_timer_pause(v->timer);
        lv_async_call(doCompletionSwap, v); // perform the swap after this refresh() returns
        return;
    }

    if (!v->useGauges && v->tiles[0].value == nullptr) return; // instruments not built yet (guard)
    switch (v->kind) {
        case ChKind::Reactor1: refreshReactor1(v); break;
        case ChKind::Reactor2: refreshReactor2(v); break;
        case ChKind::Pump: refreshPump(v); break;
        case ChKind::Iec104: refreshIec104(v); break;
        case ChKind::Sis: refreshSis(v); break;
        case ChKind::Opcua: refreshOpcua(v); break;
        case ChKind::Dam: refreshDam(v); break;
        case ChKind::Goose: refreshGoose(v); break;
        case ChKind::Forensics: refreshForensics(v); break;
        case ChKind::Trojan: refreshTrojan(v); break;
        case ChKind::Device: refreshDevice(v); break;
        case ChKind::SnmpRecovery: refreshSnmp(v); break;
        default: break;
    }
}

void onRootDeleted(lv_event_t* e) {
    auto* v = static_cast<View*>(lv_event_get_user_data(e));
    if (v->timer) lv_timer_delete(v->timer);
    delete v;
}

void startService(ChKind k) {
    switch (k) {
        case ChKind::Iec104: Iec104Service::instance().start(); break;
        case ChKind::Sis: SisService::instance().start(); break;
        case ChKind::Pump: PumpService::instance().start(); break;
        case ChKind::Opcua: OpcUaService::instance().start(); break;
        case ChKind::Dam: DamService::instance().start(); break;
        case ChKind::Goose: GooseService::instance().start(); break;
        // Defensive web challenges share DefService; #07's SQLi console is served off the same :80 web.
        case ChKind::Forensics: DefService::instance().start(); break;
        case ChKind::Trojan:
        case ChKind::Device: DefService::instance().start(); break;
        case ChKind::SnmpRecovery: SnmpService::instance().start(); break; // #08 field-device recovery
        case ChKind::NotImplemented: break; // reserved; no backend
        default: OtService::instance().start(Challenge::One); break;
    }
}

// One telemetry grid row (PARAMETER | VALUE | SAFE RANGE) in the condensed 5x8 font, with thin grid
// borders (bottom rule + column rules) in the accent colour. Returns the VALUE cell so the live
// refresh can drive it (stored as a Tile so the existing refresh*() code works unchanged).
lv_obj_t* tRow(lv_obj_t* parent, const char* p, const char* val, const char* r,
               lv_color_t pc, lv_color_t vc, lv_color_t rc, lv_color_t grid, bool last) {
    auto* row = rf::box(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_ver(row, 1, 0);
    if (!last) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, grid, 0);
    }
    auto* pl = rf::label(row, p, rf::fontTiny(), pc);
    lv_obj_set_width(pl, 112);
    lv_obj_set_style_pad_left(pl, 3, 0);
    lv_label_set_long_mode(pl, LV_LABEL_LONG_CLIP);
    auto* vl = rf::label(row, val, rf::fontTiny(), vc);
    lv_obj_set_width(vl, 72);
    lv_obj_set_style_pad_left(vl, 4, 0);
    lv_obj_set_style_border_side(vl, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(vl, 1, 0);
    lv_obj_set_style_border_color(vl, grid, 0);
    lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
    auto* rl = rf::label(row, r, rf::fontTiny(), rc);
    lv_obj_set_flex_grow(rl, 1);
    lv_obj_set_width(rl, 0);
    lv_obj_set_style_pad_left(rl, 4, 0);
    lv_obj_set_style_border_side(rl, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(rl, 1, 0);
    lv_obj_set_style_border_color(rl, grid, 0);
    lv_label_set_long_mode(rl, LV_LABEL_LONG_CLIP);
    return vl;
}

void buildInstruments(View* v, lv_obj_t* area, const ChallengeDef& def) {
    const lv_color_t accent = v->accent;
    struct Row { const char* p; const char* r; };
    Row rows[4]; int rc = 3;
    switch (v->kind) {
        case ChKind::Reactor1:
            rows[0] = {"Temperature", "20 - 80 \xC2\xB0""C"}; rows[1] = {"Pressure", "1.0 - 3.0 bar"};
            rows[2] = {"Neutron Flux", "0 - 1000 n/s"}; rows[3] = {"Reactor State", "STABLE"}; rc = 4; break;
        case ChKind::Reactor2:
            rows[0] = {"Maint Rod", "0-100"}; rows[1] = {"Unlock", "-"}; rows[2] = {"Control", "-"}; break;
        case ChKind::Pump:
            rows[0] = {"Valve", "-"}; rows[1] = {"Alert", "clear"}; rows[2] = {"State", "-"}; break;
        case ChKind::Iec104:
            rows[0] = {"Breaker", "-"}; rows[1] = {"Interrog", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::Sis:
            rows[0] = {"SIS Trip", "-"}; rows[1] = {"Checksum", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::Opcua:
            rows[0] = {"Browse", "-"}; rows[1] = {"Flag Node", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::Dam:
            rows[0] = {"Gate Pos", "= target"}; rows[1] = {"Target", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::Goose:
            rows[0] = {"Breaker", "closed"}; rows[1] = {"Last stNum", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::Forensics:
            rows[0] = {"Console", "-"}; rows[1] = {"Flag", "-"}; rows[2] = {"Finding", "-"}; break;
        case ChKind::Trojan:
            rows[0] = {"Record", "-"}; rows[1] = {"Hold", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::Device:
            rows[0] = {"Stations", "-"}; rows[1] = {"Identity", "-"}; rows[2] = {"State", "-"}; break;
        case ChKind::SnmpRecovery:
            rows[0] = {"Device Image", "last-good"}; rows[1] = {"Device Health", "HEALTHY"};
            rows[2] = {"Recovery", "verified"}; break;
        default: rc = 0; break;
    }
    lv_obj_set_width(area, LV_PCT(100));
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(area, 0, 0);
    lv_obj_set_style_border_width(area, 1, 0);
    lv_obj_set_style_border_color(area, accent, 0);
    tRow(area, "PARAMETER", "VALUE", "SAFE RANGE", accent, accent, accent, accent, false);
    for (int i = 0; i < rc; i++)
        v->tiles[i].value = tRow(area, rows[i].p, "--", rows[i].r,
                                 th::text(), th::warning(), th::textMuted(), accent, i == rc - 1);
    (void)def;
}


// A full-width dashed divider rule (label of dashes, clipped to the parent width). Negative vertical
// margins collapse the 8px glyph box so the thin dash line takes little vertical space.
lv_obj_t* dash(lv_obj_t* parent, lv_color_t color) {
    auto* d = rf::label(parent, "----------------------------------------", rf::fontSmall(), color);
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_style_margin_top(d, -2, 0);
    lv_obj_set_style_margin_bottom(d, -2, 0);
    lv_label_set_long_mode(d, LV_LABEL_LONG_CLIP);
    return d;
}

// Switch the active tab: show its panel, hide the rest, and recolour the tab chips.
// Rebuild the focus-group order so the hint rows sit directly after the HINTS chip while that tab is
// active: OVERVIEW, HINTS, [hint1, hint2,] STATUS, OPERATIONS. Then UP/DOWN naturally steps from the
// HINTS tab into its hints, and continues on to STATUS.
void applyGroupOrder(View* v) {
    if (v->grp == nullptr) return;
    for (int i = 0; i < TAB_COUNT; i++) if (v->tabBtns[i]) lv_group_remove_obj(v->tabBtns[i]);
    for (int i = 0; i < HINT_COUNT; i++) if (v->hintRows[i]) lv_group_remove_obj(v->hintRows[i]);
    lv_group_add_obj(v->grp, v->tabBtns[0]);
    lv_group_add_obj(v->grp, v->tabBtns[1]);
    lv_group_add_obj(v->grp, v->tabBtns[TAB_HINTS]);
    if (v->activeTab == TAB_HINTS)
        for (int i = 0; i < HINT_COUNT; i++) if (v->hintRows[i]) lv_group_add_obj(v->grp, v->hintRows[i]);
    lv_group_add_obj(v->grp, v->tabBtns[3]);
}

void ensureTab(View* v, int idx); // builds a deferred (heavy) tab panel on first view; see below

// Free a heavy, deferred tab panel (SYSTEM=1, STATUS=3) and null the widgets the 500ms refresh()
// touches, so at most ONE of the two is ever resident. SYSTEM + STATUS + OVERVIEW + HINTS all built
// at once drove free internal heap to the ~10 KB floor and crashed this no-PSRAM board; cycling the
// two heavy panels one-at-a-time keeps a safe margin (each is rebuilt on re-entry by ensureTab).
void freeDeferredTab(View* v, int tab) {
    if (tab == 1 && v->tabPanels[1]) {
        lv_obj_delete(v->tabPanels[1]);
        v->tabPanels[1] = nullptr;
        for (auto& t : v->tiles) t.value = nullptr;  // SYSTEM instruments gone
        v->useGauges = false;                        // refresh()'s guard now returns before any gauge deref
    } else if (tab == 3 && v->tabPanels[3]) {
        lv_obj_delete(v->tabPanels[3]);
        v->tabPanels[3] = nullptr;
        v->stStatus = nullptr; v->stElapsed = nullptr;       // refreshStatus() guards on stStatus==nullptr
        v->stEarned = nullptr; v->stDeducted = nullptr; v->stNet = nullptr;
    }
}

void setTab(View* v, int idx) {
    // Peak-heap cap: drop the heavy deferred panel we are LEAVING before building the one we are
    // entering, so SYSTEM and STATUS never coexist (that collision exhausted internal heap -> crash).
    if (v->activeTab != idx && (v->activeTab == 1 || v->activeTab == 3)) freeDeferredTab(v, v->activeTab);
    ensureTab(v, idx); // (re)build this panel if it was deferred or freed
    v->activeTab = idx;
    for (int i = 0; i < TAB_COUNT; i++) {
        if (v->tabPanels[i]) {
            if (i == idx) lv_obj_remove_flag(v->tabPanels[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(v->tabPanels[i], LV_OBJ_FLAG_HIDDEN);
        }
        const lv_color_t c = (i == idx) ? th::green() : v->accent;
        if (v->tabBtns[i]) lv_obj_set_style_border_color(v->tabBtns[i], c, 0);
        if (v->tabLbls[i]) lv_obj_set_style_text_color(v->tabLbls[i], (i == idx) ? th::green() : th::textMuted(), 0);
    }
    if (v->grp == nullptr || v->rebuilding) return;
    const bool hintsActive = (idx == TAB_HINTS);
    if (hintsActive != v->hintsInGroup) {
        if (!hintsActive && v->confirming) {           // leaving HINTS: clear any pending reveal
            v->confirming = false;
            if (v->cfBox) lv_obj_add_flag(v->cfBox, LV_OBJ_FLAG_HIDDEN);
        }
        v->hintsInGroup = hintsActive;
        v->rebuilding = true;
        applyGroupOrder(v);
        if (v->tabBtns[idx]) lv_group_focus_obj(v->tabBtns[idx]); // keep focus on the active chip
        v->rebuilding = false;
    }
}

void onTabFocused(lv_event_t* e) {
    auto* v = static_cast<View*>(lv_event_get_user_data(e));
    auto* btn = lv_event_get_target_obj(e);
    setTab(v, (int)reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn)));
}

// One bulleted objective row: a small square bullet (accent) + the objective text (condensed font).
void objRow(lv_obj_t* parent, const char* text, lv_color_t accent) {
    auto* row = rf::box(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 4, 0);
    auto* b = rf::box(row);
    lv_obj_set_size(b, 5, 5);
    lv_obj_set_style_margin_top(b, 1, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, accent, 0);
    // Objectives are essential instructions, so they use the readable 5x8 font (not the 4x6 micro).
    auto* l = rf::label(row, text, rf::fontTiny(), th::text());
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_width(l, 0);
    lv_obj_set_style_text_line_space(l, 1, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}

// TAB 1 - OVERVIEW: the challenge brief across the full content area (STORY + OBJECTIVES). The title
// already sits in the breadcrumb row above, so it is not repeated big here (that space goes to the
// readable brief). ACCESS INFO lives on the SYSTEM tab, next to the network diagram. The keypad only
// moves focus (it cannot scroll a panel), so this is laid out to fit 320x240 at readable sizes rather
// than relying on scrolling; the scrollbar is still enabled as a safety net for the longest content.
lv_obj_t* buildOverviewInfo(lv_obj_t* parent, const ChallengeDef& def, lv_color_t accent) {
    auto* d = rf::panel(parent, accent, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d, 1, 0);
    lv_obj_set_style_pad_all(d, 3, 0);
    lv_obj_set_scroll_dir(d, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(d, LV_SCROLLBAR_MODE_AUTO);

    rf::label(d, "STORY", rf::fontSmall(), accent);
    auto* story = rf::label(d, def.story, rf::fontSmall(), th::text());
    lv_label_set_long_mode(story, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(story, LV_PCT(100));
    lv_obj_set_style_text_line_space(story, 1, 0);
    dash(d, accent);
    rf::label(d, "OBJECTIVES", rf::fontSmall(), accent);
    for (int i = 0; i < def.objectiveCount; i++) objRow(d, def.objectives[i], accent);
    return d;
}

// The network entry point (PROTOCOL + ADDRESS), shown compactly on the SYSTEM tab. Two tight 5x8
// lines (no separate heading/divider) so it always fits under the diagram + telemetry at 320x240.
// No-op when the challenge has no network entry point (e.g. evidence-only).
void accessInfoBlock(lv_obj_t* parent, const ChallengeDef& def, lv_color_t accent) {
    if (!(def.protocol[0] && def.access[0])) return;
    char line[88];
    snprintf(line, sizeof(line), "ACCESS >> %s  (%s)", def.access, def.protocol);
    auto* a = rf::label(parent, line, rf::fontTiny(), accent);
    lv_obj_set_width(a, LV_PCT(100));
    lv_obj_set_style_margin_top(a, 2, 0);
    lv_label_set_long_mode(a, LV_LABEL_LONG_WRAP);
}

// One diagram node: a line-art icon with its name (and optional address) stacked beneath it.
void iconNode(lv_obj_t* parent, const lv_image_dsc_t* img, const char* name, const char* addr) {
    auto* col = rf::box(parent);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 1, 0);
    auto* im = lv_image_create(col);
    lv_image_set_src(im, img);
    lv_image_set_scale(im, 256); // native size: leaves room for the readable 4-row telemetry table
    lv_obj_set_size(im, 38, 30);
    rf::label(col, name, rf::fontSmall(), th::text());
    if (addr && addr[0]) rf::label(col, addr, rf::fontTiny(), th::textMuted());
}

// A red directional connector between two diagram nodes (horizontal line + arrowhead).
void redArrow(lv_obj_t* parent) {
    auto* a = rf::box(parent);
    lv_obj_set_flex_grow(a, 1);
    lv_obj_set_height(a, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(a, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(a, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_bottom(a, 14, 0); // align to icon mid-height
    auto* bar = rf::box(a);
    lv_obj_set_height(bar, 2);
    lv_obj_set_flex_grow(bar, 1);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, th::offensive(), 0); // arrows are always red (per the reference)
    rf::label(a, ">", rf::fontSmall(), th::offensive());
}

// TAB 2 - SYSTEM OVERVIEW: the system diagram (red directional arrows) + live telemetry, full width.
lv_obj_t* buildOverviewSystem(View* v, lv_obj_t* parent, const ChallengeDef& def, lv_color_t accent) {
    auto* d = rf::panel(parent, accent, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d, 1, 0);
    lv_obj_set_style_pad_all(d, 3, 0);
    lv_obj_set_scroll_dir(d, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(d, LV_SCROLLBAR_MODE_AUTO);

    const bool defensive = def.category == Category::Defensive;
    auto* strip = rf::box(d);
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_height(strip, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(strip, 3, 0);
    if (defensive) {
        // Defensive: analyst watching the plant network - no reactor, no fabricated telemetry.
        iconNode(strip, &icon_you, "ANALYST", "10.0.1.50");
        redArrow(strip);
        iconNode(strip, &icon_plc, "PLANT NET", "");
    } else {
        iconNode(strip, &icon_you, "YOU", "10.0.1.50");
        redArrow(strip);
        iconNode(strip, &icon_plc, "PLC", "10.0.1.1");
        redArrow(strip);
        iconNode(strip, &icon_reactor, assetName(v->kind), "");
    }

    dash(d, accent);
    auto* area = rf::box(d);
    lv_obj_set_width(area, LV_PCT(100));
    lv_obj_set_height(area, LV_SIZE_CONTENT);
    if (v->kind == ChKind::Reactor1) {
        // Three semicircular safe-band dials (the reusable SemiGauge instrument), driven live from
        // the reactor snapshot in refreshReactor1(). Full-width row, three across at 82px.
        lv_obj_set_flex_flow(area, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(area, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        // Captions match the web HMI gauge titles (CORE TEMP / PRESSURE / NEUTRON FLUX). "NEUTRON
        // FLUX" is too wide for the 82px dial caption, so flux keeps the short form.
        v->rGauge[0] = in::semiGauge(area, "CORE TEMP", 82);
        v->rGauge[1] = in::semiGauge(area, "PRESSURE", 82);
        v->rGauge[2] = in::semiGauge(area, "FLUX", 82);
        v->useGauges = true;
    } else {
        buildInstruments(v, area, def);
    }
    // Network entry point lives here (next to the diagram), moved off the OVERVIEW tab for space.
    accessInfoBlock(d, def, accent);
    return d;
}

// --- HINTS tab -----------------------------------------------------------------------------------

// Authored, per-challenge hint tiers that replace what the evidence notes used to hand over for free
// (see the correction write-up). idx 0 = cheap "GET STARTED", idx 1 = pricier "IMPORTANT LEAD".
// Returns nullptr to fall back to the generic, objective-derived hint body below.
const char* customHint(int number, int idx) {
    switch (number) {
        case 1: // Historian SQL Injection: hint 0 reveals WHERE the console is (the brief no longer
                // gives it away); hint 1 reveals the SQLi technique. Neither hint reveals the flag.
            return idx == 0 ? "The historian has its own console at 10.0.1.1/hist (off the main nav). Its login historian/readonly is in the service config - see robots.txt, then /hist-config."
                 : idx == 1 ? "Filter-by-Source is injectable (a quote breaks it). UNION SELECT with the matching column count; list tables via sqlite_master (type='table'); then read the flags table."
                 : nullptr;
        case 3: // Trojan Historian: hint 0 = how to find the record; hint 1 = the decoy-gap insight.
            return idx == 0 ? "Compare the two historian exports in webmail (live trend vs nightly backup). Filter the backup to the same tag as the live trend first, then diff the sequence numbers."
                 : idx == 1 ? "Not every gap in the live trend is the deletion - some seqs are missing from the backup too (comms outages). Restore the one seq that is in the backup but absent from the live trend."
                 : nullptr;
        case 8: // Maintenance Override: nudge toward the hidden unlock-expiry timing register.
            return idx == 0 ? "The unlock's behaviour suggests it has a lifespan - is that remaining time exposed on a register anywhere?"
                 : nullptr;
        default: return nullptr;
    }
}

void hintBodyText(const ChallengeDef& def, int idx, char* out, size_t n) {
    if (const char* ch = customHint(def.number, idx)) {
        snprintf(out, n, "%s", ch);
        return;
    }
    if (idx == 0) {
        snprintf(out, n, "Start by enumerating the %s service at %s. %s.",
                 def.protocol, def.access,
                 def.objectiveCount > 0 ? def.objectives[0] : "Map the target");
    } else {
        snprintf(out, n, "%s. Then %s.",
                 def.objectiveCount > 1 ? def.objectives[1] : "Look closer at the traffic",
                 def.objectiveCount > 2 ? def.objectives[2] : "press the attack home");
    }
}

void updateHintDetail(View* v) {
    const int i = v->hintSel;
    const HintDef& h = CHALLENGE_HINTS[i];
    char t[32]; snprintf(t, sizeof(t), "HINT %d: %s", i + 1, h.title);
    lv_label_set_text(v->hdTitle, t);
    char c[16]; snprintf(c, sizeof(c), "[-%d PTS]", h.cost);
    lv_label_set_text(v->hdCost, c);
    if (hintRevealed(v->number, i)) {
        char body[288]; hintBodyText(*v->def, i, body, sizeof(body));  // fits the longer #07 SQLi hints
        lv_label_set_text(v->hdBody, body);
        lv_obj_set_style_text_color(v->hdBody, th::text(), 0);
        if (v->cfBox) lv_obj_add_flag(v->cfBox, LV_OBJ_FLAG_HIDDEN);
    } else if (v->confirming) {
        lv_label_set_text(v->hdBody, "Reveal this hint?");
        lv_obj_set_style_text_color(v->hdBody, th::warning(), 0);
        if (v->cfBox) lv_obj_remove_flag(v->cfBox, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(v->hdBody, "Hidden. Press SELECT to reveal (this costs points).");
        lv_obj_set_style_text_color(v->hdBody, th::textMuted(), 0);
        if (v->cfBox) lv_obj_add_flag(v->cfBox, LV_OBJ_FLAG_HIDDEN);
    }
}

void cancelConfirm(View* v) {
    if (!v->confirming) return;
    v->confirming = false;
}

void onHintFocused(lv_event_t* e) {
    auto* v = static_cast<View*>(lv_event_get_user_data(e));
    auto* row = lv_event_get_target_obj(e);
    const int idx = (int)reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
    if (idx != v->hintSel) cancelConfirm(v); // moving to another hint cancels a pending reveal
    v->hintSel = idx;
    updateHintDetail(v);
}

// SELECT on a hint: first press arms the reveal (shows the warning), a second press on the same hint
// confirms it and deducts the points. Moving to another hint cancels.
void onHintClicked(lv_event_t* e) {
    auto* v = static_cast<View*>(lv_event_get_user_data(e));
    auto* row = lv_event_get_target_obj(e);
    v->hintSel = (int)reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
    if (hintRevealed(v->number, v->hintSel)) { updateHintDetail(v); return; }
    if (v->confirming) {
        revealHint(v->number, v->hintSel); // second press -> reveal and deduct
        v->confirming = false;
    } else {
        v->confirming = true;              // first press -> arm the confirm
    }
    updateHintDetail(v);
}

lv_obj_t* makeHintRow(View* v, lv_obj_t* parent, int idx) {
    const HintDef& h = CHALLENGE_HINTS[idx];
    auto* row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_user_data(row, reinterpret_cast<void*>((uintptr_t)idx));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 3, 0);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, v->accent, 0);
    lv_obj_set_style_border_width(row, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(row, th::green(), LV_STATE_FOCUS_KEY);
    // The hint-list column is narrow (two columns on 320px), so the full title "IMPORTANT LEAD" does
    // not fit at unscii-8. Drop the "HINT N:" prefix (the position implies the number) and use the
    // condensed 5x8 font so the whole title + cost are always visible without clipping.
    rf::label(row, ">", rf::fontTiny(), v->accent);
    auto* nm = rf::label(row, h.title, rf::fontTiny(), th::text());
    lv_obj_set_flex_grow(nm, 1);
    lv_obj_set_width(nm, 0);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_CLIP);
    char c[10]; snprintf(c, sizeof(c), "[-%d]", h.cost);
    rf::label(row, c, rf::fontTiny(), v->accent);
    lv_obj_add_event_cb(row, onHintFocused, LV_EVENT_FOCUSED, v);
    lv_obj_add_event_cb(row, onHintClicked, LV_EVENT_SHORT_CLICKED, v);
    v->hintRows[idx] = row;
    return row;
}

lv_obj_t* buildHints(View* v, lv_obj_t* parent, lv_color_t accent) {
    auto* panel = rf::box(parent);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(panel, 4, 0);

    // left: hint list
    auto* left = rf::panel(panel, accent, 0, LV_PCT(100));
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 3, 0);
    rf::label(left, "HINTS", rf::fontSmall(), accent);
    dash(left, accent);
    for (int i = 0; i < HINT_COUNT; i++) makeHintRow(v, left, i);

    // right: selected-hint detail
    auto* right = rf::panel(panel, accent, 0, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 2, 0);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);
    auto* hrow = rf::box(right);
    lv_obj_set_width(hrow, LV_PCT(100));
    lv_obj_set_height(hrow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hrow, 3, 0);
    // Condensed font + a grow+clip title so the title can never overlap the cost chip (the narrow
    // right column could not fit "HINT N: IMPORTANT LEAD" + "[-N PTS]" at unscii-8).
    v->hdTitle = rf::label(hrow, "", rf::fontTiny(), accent);
    lv_obj_set_flex_grow(v->hdTitle, 1);
    lv_obj_set_width(v->hdTitle, 0);
    lv_label_set_long_mode(v->hdTitle, LV_LABEL_LONG_CLIP);
    v->hdCost = rf::label(hrow, "", rf::fontTiny(), th::green());
    dash(right, accent);
    v->hdBody = rf::label(right, "", rf::fontSmall(), th::text());
    lv_label_set_long_mode(v->hdBody, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(v->hdBody, LV_PCT(100));
    // confirm box (hidden until the player presses SELECT on a locked hint)
    auto* cf = rf::panel(right, th::warning(), LV_PCT(100), 0);
    v->cfBox = cf;
    lv_obj_set_flex_flow(cf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cf, 3, 0);
    lv_obj_set_style_margin_top(cf, 3, 0);
    rf::label(cf, "This permanently deducts", rf::fontSmall(), th::warning());
    rf::label(cf, "points from your score.", rf::fontSmall(), th::warning());
    rf::label(cf, "SELECT again = CONFIRM", rf::fontSmall(), th::green());
    rf::label(cf, "move away = CANCEL", rf::fontSmall(), v->accent);
    lv_obj_add_flag(cf, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

// --- STATUS tab ----------------------------------------------------------------------------------

lv_obj_t* kv(lv_obj_t* p, const char* k, const char* val, lv_color_t accent) {
    auto* row = rf::box(p);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 3, 0);
    // Two-column STATUS: the columns are narrow, so keys like "DIFFICULTY" and values like
    // "NOT STARTED" only fit in the condensed 5x8 font (unscii-8 clipped both).
    auto* kl = rf::label(row, k, rf::fontTiny(), accent);
    lv_obj_set_width(kl, 60);
    lv_label_set_long_mode(kl, LV_LABEL_LONG_CLIP);
    auto* vl = rf::label(row, val, rf::fontTiny(), th::text());
    lv_obj_set_flex_grow(vl, 1);
    lv_obj_set_width(vl, 0);
    lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
    return vl;
}

lv_obj_t* buildStatus(View* v, lv_obj_t* parent, const ChallengeDef& def, lv_color_t accent) {
    const bool offensive = def.category == Category::Offensive;
    auto* panel = rf::box(parent);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(panel, 4, 0);

    // left column: challenge status + objectives
    auto* left = rf::panel(panel, accent, 0, LV_PCT(100));
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 1, 0);
    lv_obj_set_scroll_dir(left, LV_DIR_VER);
    rf::label(left, "CHALLENGE STATUS", rf::fontSmall(), accent);
    dash(left, accent);
    // (challenge name omitted here - it is already shown in the breadcrumb header above.)
    kv(left, "CATEGORY", offensive ? "OFFENSIVE" : "DEFENSIVE", accent);
    kv(left, "DIFFICULTY", difficultyLabel(def.difficulty), accent);
    char pts[8]; snprintf(pts, sizeof(pts), "%d", def.points);
    kv(left, "POINTS", pts, accent);
    v->stStatus = kv(left, "STATUS", "", accent);
    v->stElapsed = kv(left, "ELAPSED", "00:00", accent);

    // right column: points + recent activity
    auto* right = rf::panel(panel, accent, 0, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 2, 0);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);
    rf::label(right, "POINTS", rf::fontSmall(), accent);
    kv(right, "TOTAL", pts, accent);
    v->stEarned = kv(right, "EARNED", "0", accent);
    v->stDeducted = kv(right, "HINTS", "0", accent);
    v->stNet = kv(right, "NET", "0", accent);
    dash(right, accent);
    rf::label(right, "RECENT ACTIVITY", rf::fontSmall(), accent);
    rf::label(right, "- session started", rf::fontTiny(), th::textMuted());
    rf::label(right, "- services probed", rf::fontTiny(), th::textMuted());
    rf::label(right, "- traffic observed", rf::fontTiny(), th::textMuted());
    return panel;
}

void refreshStatus(View* v) {
    if (v->stStatus == nullptr || v->def == nullptr) return;
    const ChallengeDef& d = *v->def;
    const ChStatus st = challengeStatus(d);
    lv_label_set_text(v->stStatus, statusLabel(st));
    lv_obj_set_style_text_color(v->stStatus,
        st == ChStatus::Completed ? th::green() : st == ChStatus::InProgress ? th::warning() : th::textMuted(), 0);
    const uint32_t s = (lv_tick_get() - v->startTick) / 1000;
    char t[16]; snprintf(t, sizeof(t), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    lv_label_set_text(v->stElapsed, t);
    const int earned = (st == ChStatus::Completed) ? d.points : 0;
    const int ded = hintPenaltyFor(v->number);
    char e[20];
    snprintf(e, sizeof(e), "%d", earned); lv_label_set_text(v->stEarned, e);
    snprintf(e, sizeof(e), "-%d", ded); lv_label_set_text(v->stDeducted, e);
    const int net = earned - ded < 0 ? 0 : earned - ded;
    snprintf(e, sizeof(e), "%d / %d", net, d.points); lv_label_set_text(v->stNet, e);
}

// Build a deferred tab panel the first time it is shown. The heavy SYSTEM panel (tab 1: reactor
// diagram + live instruments) and the STATUS panel (tab 3) are NOT built in createWidgets; building
// all four panels at once drove free internal heap to ~0 on this no-PSRAM board, which starved the
// single-task HTTP server during the screen transition (HTTP stalled/failed while any page was being
// served; ping survived). Deferring the two heavy panels keeps the open-time peak low so the AP + web
// stay responsive on every screen. The other panels (OVERVIEW info, HINTS) stay eager. All the
// refresh/setTab paths already null-guard an unbuilt panel, so a deferred panel is safe until shown.
void ensureTab(View* v, int idx) {
    if (v->content == nullptr || v->def == nullptr || v->tabPanels[idx] != nullptr) return;
    if (idx == 1) v->tabPanels[1] = buildOverviewSystem(v, v->content, *v->def, v->accent);
    else if (idx == 3) v->tabPanels[3] = buildStatus(v, v->content, *v->def, v->accent);
    // A newly built panel starts hidden; setTab (our only caller) unhides the active one right after.
    if (v->tabPanels[idx]) lv_obj_add_flag(v->tabPanels[idx], LV_OBJ_FLAG_HIDDEN);
}

void createWidgets(lv_obj_t* parent, void*) {
    auto* v = new View();
    v->number = selectedChallenge();
    const ChallengeDef& def = challengeByNumber(v->number);
    v->def = &def;
    v->kind = def.kind;
    v->startTick = lv_tick_get();
    v->wasCompleted = (challengeStatus(def) == ChStatus::Completed); // suppress the auto-complete edge
                                                                     // when opening an already-solved one
    const bool offensive = def.category == Category::Offensive;
    const lv_color_t accent = offensive ? th::offensive() : th::defensive();
    const char* catName = offensive ? "OFFENSIVE" : "DEFENSIVE";
    v->accent = accent;
    th::installSystemTheme(lv_obj_get_display(parent));
    startService(v->kind);

    auto* root = rf::box(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, th::background(), 0);
    lv_obj_set_style_pad_all(root, 6, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 3, 0);
    v->root = root;
    lv_obj_add_event_cb(root, onRootDeleted, LV_EVENT_DELETE, v);

    // Row 1: title + score chip.
    rf::headerBar(root, netScore(), SCORE_MAX, accent);

    // Row 2: breadcrumb  "> CATEGORY > NAME"  +  [ DIFF ]  POINTS: n
    auto* head = rf::box(root);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 4, 0);
    char crumb[20]; snprintf(crumb, sizeof(crumb), "> %s >", catName);
    rf::label(head, crumb, rf::fontTiny(), accent);
    auto* nm = rf::label(head, displayTitle(def), rf::fontSmall(), th::text()); // name stays prominent (unscii-8)
    lv_obj_set_flex_grow(nm, 1);
    lv_obj_set_width(nm, 0);
    lv_obj_set_style_text_letter_space(nm, -1, 0);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_CLIP);
    char diff[14]; snprintf(diff, sizeof(diff), "[ %s ]", difficultyLabel(def.difficulty));
    rf::label(head, diff, rf::fontTiny(), th::green());
    char hp[16]; snprintf(hp, sizeof(hp), "POINTS: %d", def.points);
    rf::label(head, hp, rf::fontTiny(), th::textMuted());

    // Row 3: tab bar (four focusable chips; arrows move between them and switch the panel).
    auto* tabs = rf::box(root);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_height(tabs, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 3, 0);
    for (int i = 0; i < TAB_COUNT; i++) {
        auto* b = lv_button_create(tabs);
        lv_obj_remove_style_all(b);
        lv_obj_set_user_data(b, reinterpret_cast<void*>((uintptr_t)i));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, accent, 0);
        lv_obj_set_style_border_width(b, 3, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_color(b, th::green(), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_pad_ver(b, 3, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto* tl = rf::label(b, TAB_NAMES[i], rf::fontSmall(), th::textMuted()); // short labels fit the readable font
        lv_label_set_long_mode(tl, LV_LABEL_LONG_CLIP);
        lv_obj_add_event_cb(b, onTabFocused, LV_EVENT_FOCUSED, v);
        v->tabBtns[i] = b;
        v->tabLbls[i] = tl;
    }

    // Row 4: content area; one panel per tab, stacked (only the active one is shown).
    auto* content = rf::box(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    v->content = content; // kept so the deferred SYSTEM/STATUS panels can be built on first view

    // New tab order: OVERVIEW (info, full width) | SYSTEM OVERVIEW (diagram + telemetry) | HINTS | STATUS
    // Build only the panels needed up front: OVERVIEW (the default visible tab) and HINTS (its group/
    // reveal state is wired here). The heavy SYSTEM panel (tab 1) and STATUS (tab 3) build lazily via
    // ensureTab() on first view -- building all four at once exhausted internal heap and stalled the
    // HTTP server during the screen transition (see ensureTab).
    v->tabPanels[0] = buildOverviewInfo(content, def, accent);
    v->tabPanels[2] = buildHints(v, content, accent);
    v->grp = lv_group_get_default();
    // lv_button_create auto-adds to the default group; pull the hint rows back out so only the tab
    // chips are navigable until the HINTS tab re-inserts its rows (right after the HINTS chip).
    if (v->grp) {
        for (int i = 0; i < HINT_COUNT; i++) if (v->hintRows[i]) lv_group_remove_obj(v->hintRows[i]);
    }
    updateHintDetail(v);

    // Row 5: footer control hints (condensed font so the full reference text fits one line).
    auto* foot = rf::box(root);
    lv_obj_set_width(foot, LV_PCT(100));
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(foot, 2, 0);
    lv_obj_set_style_border_color(foot, accent, 0);
    lv_obj_set_style_pad_top(foot, 2, 0);
    // The keypad only moves focus (joystick in any direction = next/prev in the tab + hint ring);
    // there is no panel-scroll axis, so the hints are truthful: navigate, act, return.
    const char* fkey[3] = {"[JOYSTICK]", "[SELECT]", "[BACK]"};
    const char* fact[3] = {"NAVIGATE", "ACTION", "RETURN"};
    for (int i = 0; i < 3; i++) {
        auto* grp = rf::box(foot);
        lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(grp, 3, 0);
        rf::label(grp, fkey[i], rf::fontTiny(), i == 2 ? accent : th::green());
        rf::label(grp, fact[i], rf::fontTiny(), th::text());
    }

    setTab(v, 0);
    lv_obj_update_layout(root);
    lv_group_focus_obj(v->tabBtns[0]);
    lv_obj_add_state(v->tabBtns[0], LV_STATE_FOCUS_KEY);

    v->timer = lv_timer_create(refresh, REFRESH_MS, v);
    refresh(v->timer);
}

int32_t appMain(int, char*[]) {
    const AppInstanceId instance_id = app_scheduler_current_app_id();
    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(instance_id, createWidgets, nullptr);

    bool close = false;
    while (!close) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) close = true;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const AppManifest challenge_manifest = {
    .id = "vanguard.challenge",
    .name = "Challenge",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::ui
