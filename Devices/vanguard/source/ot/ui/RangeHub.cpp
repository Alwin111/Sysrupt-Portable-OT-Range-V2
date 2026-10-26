// SPDX-License-Identifier: Apache-2.0
#include "RangeHub.h"
#include "RetroTheme.h"
#include "../OtService.h"
#include "../sim/PlantModel.h"

#include <app/event.h>
#include <app/scheduler.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <cstdio>

// The badge's Sysrupt-style scoreboard: a retro state machine on the 320x240 screen that mirrors
// the Sysrupt OT Range display (boot -> attract -> progress -> victory), adapted to the reactor
// challenge and to Wi-Fi onboarding. It also owns the running range (starts OtService, which brings
// up the AP + reactor).
namespace ot::range {

namespace {

namespace r = ot::retro;

constexpr uint32_t BOOT_MS = 4500;
constexpr uint32_t TICK_MS = 250;
constexpr auto* SSID = "sysrupt ot";

// The attack stages shown in the progress grid (reactor range, Sysrupt-styled).
struct Stage {
    const char* code;
    const char* name;
    uint16_t points;
};
constexpr Stage STAGES[] = {
    { "01", "RECON / JOIN", 100 },
    { "02", "MODBUS MAP", 300 },
    { "03", "SETPOINT SAFETY", 400 },
    { "04", "UNLOCK TOCTOU", 600 },
    { "05", "FULL COMPROMISE", 1000 },
};
constexpr int STAGE_COUNT = sizeof(STAGES) / sizeof(STAGES[0]);
constexpr uint16_t TOTAL_POINTS = 2400;

enum class Screen { Boot, Idle, Progress, Victory };

struct Hub {
    // screens
    lv_obj_t* boot;
    lv_obj_t* idle;
    lv_obj_t* progress;
    lv_obj_t* victory;
    // boot
    lv_obj_t* bootBar;
    lv_obj_t* bootText;
    // idle
    lv_obj_t* idlePrompt;
    lv_obj_t* idleClients;
    // progress
    lv_obj_t* cells[STAGE_COUNT];
    lv_obj_t* hpFill;
    lv_obj_t* scoreLabel;
    lv_obj_t* objLabel;
    lv_obj_t* progClients;
    // victory
    lv_obj_t* victoryFlag;

    lv_timer_t* timer;
    uint32_t startMs;
    Screen screen;
    int blink;
    // Progress latches forward only (a CTF scoreboard never takes points back): once a challenge has
    // been solved we keep its stage lit even if the live plant later leaves the band or scrams.
    bool ch1Ever;
    bool ch2Ever;
    bool victoryLatched;
    char victoryFlagStr[40];
    uint32_t session; // OtService session id this board reflects; a change means a fresh player
};

uint32_t nowMs() { return lv_tick_get(); }

lv_obj_t* box(lv_obj_t* parent) {
    auto* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* screenBase(lv_obj_t* parent) {
    auto* s = box(parent);
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, r::black(), 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
    return s;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// region screens

void buildBoot(Hub* h, lv_obj_t* parent) {
    auto* s = screenBase(parent);
    h->boot = s;
    lv_obj_set_style_pad_row(s, 6, 0);

    auto* logo = label(s, "VANGUARD", lvgl_get_text_font(FONT_SIZE_LARGE), r::red());
    lv_obj_set_style_text_letter_space(logo, 6, 0);
    auto* sub = label(s, "OT RANGE", lvgl_get_text_font(FONT_SIZE_DEFAULT), r::green());
    lv_obj_set_style_text_letter_space(sub, 4, 0);

    h->bootBar = lv_bar_create(s);
    lv_obj_set_size(h->bootBar, 200, 8);
    lv_bar_set_range(h->bootBar, 0, 100);
    lv_obj_set_style_bg_color(h->bootBar, r::grayDark(), 0);
    lv_obj_set_style_bg_color(h->bootBar, r::red(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(h->bootBar, 0, 0);
    lv_obj_set_style_radius(h->bootBar, 0, LV_PART_INDICATOR);

    h->bootText = label(s, "", lvgl_get_text_font(FONT_SIZE_SMALL), r::green());
}

void buildIdle(Hub* h, lv_obj_t* parent) {
    auto* s = screenBase(parent);
    h->idle = s;
    lv_obj_set_style_pad_row(s, 4, 0);

    auto* logo = label(s, "VANGUARD", lvgl_get_text_font(FONT_SIZE_LARGE), r::red());
    lv_obj_set_style_text_letter_space(logo, 5, 0);
    label(s, "PORTABLE OT RANGE", lvgl_get_text_font(FONT_SIZE_SMALL), r::grayLight());

    h->idlePrompt = label(s, "JOIN WI-FI TO START", lvgl_get_text_font(FONT_SIZE_DEFAULT), r::amber());
    lv_obj_set_style_text_letter_space(h->idlePrompt, 2, 0);
    lv_obj_set_style_pad_top(h->idlePrompt, 6, 0);

    // Info box (how to connect)
    auto* infobox = box(s);
    lv_obj_set_size(infobox, 240, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(infobox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(infobox, 6, 0);
    lv_obj_set_style_pad_row(infobox, 1, 0);
    lv_obj_set_style_border_width(infobox, 1, 0);
    lv_obj_set_style_border_color(infobox, r::green(), 0);
    lv_obj_set_style_bg_opa(infobox, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(infobox, r::panel(), 0);

    char line[48];
    snprintf(line, sizeof(line), "SSID: %s", SSID);
    label(infobox, line, lvgl_get_text_font(FONT_SIZE_SMALL), r::white());
    label(infobox, "PASS: (open network)", lvgl_get_text_font(FONT_SIZE_SMALL), r::white());
    label(infobox, "SCAN: nmap 10.0.1.1", lvgl_get_text_font(FONT_SIZE_SMALL), r::green());
    label(infobox, "HMI:  http://10.0.1.1", lvgl_get_text_font(FONT_SIZE_SMALL), r::green());
    label(infobox, "ENGINEERING ACCESS REQUIRED", lvgl_get_text_font(FONT_SIZE_SMALL), r::amber());

    h->idleClients = label(s, "WAITING FOR OPERATOR...", lvgl_get_text_font(FONT_SIZE_SMALL), r::grayMid());
    lv_obj_set_style_pad_top(h->idleClients, 4, 0);
}

void buildProgress(Hub* h, lv_obj_t* parent) {
    auto* s = screenBase(parent);
    h->progress = s;
    lv_obj_remove_flag(s, LV_OBJ_FLAG_HIDDEN); // flex align override below
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s, 6, 0);
    lv_obj_set_style_pad_row(s, 3, 0);

    // Header: title + score
    auto* head = box(s);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto* title = label(head, "OP: REACTOR", lvgl_get_text_font(FONT_SIZE_DEFAULT), r::red());
    lv_obj_set_style_text_letter_space(title, 2, 0);
    h->scoreLabel = label(head, "0", lvgl_get_text_font(FONT_SIZE_DEFAULT), r::green());

    // Stage grid
    auto* grid = box(s);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(grid, 2, 0);
    for (int i = 0; i < STAGE_COUNT; i++) {
        auto* cell = box(grid);
        lv_obj_set_size(cell, 54, 34);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, r::grayDark(), 0);
        auto* num = label(cell, STAGES[i].code, lvgl_get_text_font(FONT_SIZE_DEFAULT), r::grayDark());
        lv_obj_set_user_data(cell, num);
        h->cells[i] = cell;
    }

    // Current objective line
    h->objLabel = label(s, "", lvgl_get_text_font(FONT_SIZE_SMALL), r::amber());
    lv_obj_set_style_pad_top(h->objLabel, 2, 0);

    // HP (plant health) bar
    auto* hpRow = box(s);
    lv_obj_set_size(hpRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hpRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hpRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hpRow, 4, 0);
    label(hpRow, "PLANT", lvgl_get_text_font(FONT_SIZE_SMALL), r::pink());
    auto* hpBar = box(hpRow);
    lv_obj_set_size(hpBar, 210, 8);
    lv_obj_set_style_bg_opa(hpBar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hpBar, r::grayDark(), 0);
    lv_obj_set_flex_grow(hpBar, 1);
    h->hpFill = box(hpBar);
    lv_obj_set_size(h->hpFill, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(h->hpFill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(h->hpFill, r::green(), 0);

    h->progClients = label(s, "", lvgl_get_text_font(FONT_SIZE_SMALL), r::grayMid());
}

void buildVictory(Hub* h, lv_obj_t* parent) {
    auto* s = screenBase(parent);
    h->victory = s;
    lv_obj_set_style_bg_color(s, r::red(), 0);
    lv_obj_set_style_pad_row(s, 6, 0);
    label(s, "SYSTEM", lvgl_get_text_font(FONT_SIZE_LARGE), r::black());
    label(s, "COMPROMISED", lvgl_get_text_font(FONT_SIZE_LARGE), r::black());
    h->victoryFlag = label(s, "", lvgl_get_text_font(FONT_SIZE_SMALL), r::white());
}

// endregion

void show(Hub* h, Screen scr) {
    if (h->screen == scr) {
        return;
    }
    h->screen = scr;
    lv_obj_t* all[] = { h->boot, h->idle, h->progress, h->victory };
    lv_obj_t* want = scr == Screen::Boot ? h->boot : scr == Screen::Idle ? h->idle
                   : scr == Screen::Progress ? h->progress : h->victory;
    for (auto* o : all) {
        if (o == want) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Reads the flag out of the reactor's input registers (revealed once solved).
void readFlagInto(char* out, size_t cap, uint16_t base, uint16_t len) {
    size_t n = 0;
    for (uint16_t i = 0; i < len && n + 2 < cap; i++) {
        uint16_t w = OtService::instance().readInput(base + i);
        char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
        if (hi) out[n++] = hi;
        if (lo) out[n++] = lo;
    }
    out[n] = '\0';
}

void updateProgress(Hub* h, const PlantSnapshot& s, uint8_t clients, const OtService::Metrics& m) {
    const bool solved[STAGE_COUNT] = {
        clients >= 1,                      // 01 recon/join
        m.mappedInput && m.mappedHolding,  // 02 modbus map: enumerated sensors (FC04) + actuators (FC03)
        h->ch1Ever,                        // 03 setpoint safety (latched)
        h->ch2Ever,                        // 04 unlock toctou (latched)
        h->ch1Ever && h->ch2Ever,          // 05 full compromise
    };
    int current = STAGE_COUNT;
    uint16_t score = 0;
    for (int i = 0; i < STAGE_COUNT; i++) {
        if (solved[i]) {
            score += STAGES[i].points;
        } else if (current == STAGE_COUNT) {
            current = i;
        }
        lv_color_t col = solved[i] ? r::green() : (i == current ? r::amber() : r::grayDark());
        lv_obj_set_style_border_color(h->cells[i], col, 0);
        auto* num = static_cast<lv_obj_t*>(lv_obj_get_user_data(h->cells[i]));
        lv_obj_set_style_text_color(num, col, 0);
    }
    lv_label_set_text_fmt(h->scoreLabel, "%u/%u", (unsigned)score, (unsigned)TOTAL_POINTS);

    if (current < STAGE_COUNT) {
        lv_label_set_text_fmt(h->objLabel, "> %s", STAGES[current].name);
    } else {
        lv_label_set_text(h->objLabel, "> ALL OBJECTIVES COMPLETE");
    }

    // Plant health: full when in the safe band, zero on scram.
    int hp = 100;
    if (s.scram) hp = 0;
    else {
        hp = ((s.safe_band & 1) ? 34 : 0) + ((s.safe_band & 2) ? 33 : 0) + ((s.safe_band & 4) ? 33 : 0);
    }
    lv_obj_set_width(h->hpFill, LV_PCT(hp));
    lv_obj_set_style_bg_color(h->hpFill, hp > 66 ? r::green() : hp > 33 ? r::amber() : r::pink(), 0);

    lv_label_set_text_fmt(h->progClients, "clients:%u   core:%d.%dC   AP:%s",
                          (unsigned)clients, s.core_temp / 10, (s.core_temp % 10 + 10) % 10, SSID);
}

void tick(lv_timer_t* timer) {
    auto* h = static_cast<Hub*>(lv_timer_get_user_data(timer));
    const uint32_t elapsed = nowMs() - h->startMs;
    h->blink++;

    // Boot sequence
    if (elapsed < BOOT_MS) {
        show(h, Screen::Boot);
        int pct = (int)(elapsed * 100 / BOOT_MS);
        lv_bar_set_value(h->bootBar, pct, LV_ANIM_OFF);
        const char* lines[] = { "init network stack...", "bringing up SoftAP...",
                                "arming reactor PLC...", "starting IDS...", "READY" };
        lv_label_set_text(h->bootText, lines[(pct / 20) % 5]);
        return;
    }

    const auto net = OtService::instance().networkStatus();
    // Drive the reactor's player-presence gate (freezes physics while idle; reseeds on a fresh join).
    OtService::instance().setPlayerPresent(net.clients >= 1);
    const auto snap = OtService::instance().snapshot();
    const auto met = OtService::instance().metrics();

    // A new session means a fresh player (the service reseeded the plant): reset the board.
    if (met.sessionId != h->session) {
        h->session = met.sessionId;
        h->ch1Ever = false;
        h->ch2Ever = false;
        h->victoryLatched = false;
        h->victoryFlagStr[0] = '\0';
    }

    // Latch solved challenges forward. Each challenge can be solved independently on the one running
    // reactor; victory is when both have been solved at some point this session (not necessarily at
    // once, since solving CH2 drives the rod out of CH1's safe band).
    if (snap.ch1_solved) h->ch1Ever = true;
    if (snap.ch2_solved) {
        h->ch2Ever = true;
        if (!h->victoryFlagStr[0]) {
            readFlagInto(h->victoryFlagStr, sizeof(h->victoryFlagStr), reg::IR_CH2_FLAG, reg::IR_CH2_FLAG_LEN);
        }
    }
    if (h->ch1Ever && h->ch2Ever) {
        h->victoryLatched = true;
    }
    if (h->victoryLatched) {
        show(h, Screen::Victory);
        lv_label_set_text(h->victoryFlag, h->victoryFlagStr);
        return;
    }

    if (net.clients == 0) {
        show(h, Screen::Idle);
        // Blink the prompt (~every other tick).
        lv_obj_set_style_opa(h->idlePrompt, (h->blink & 1) ? LV_OPA_COVER : LV_OPA_30, 0);
        return;
    }

    show(h, Screen::Progress);
    updateProgress(h, snap, net.clients, met);
}

void onRootDeleted(lv_event_t* e) {
    auto* h = static_cast<Hub*>(lv_event_get_user_data(e));
    if (h->timer) lv_timer_delete(h->timer);
    delete h;
}

void createWidgets(lv_obj_t* parent, void*) {
    auto* h = new Hub();
    h->startMs = nowMs();
    h->screen = (Screen)-1;

    auto* root = box(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, r::black(), 0);
    lv_obj_add_event_cb(root, onRootDeleted, LV_EVENT_DELETE, h);

    buildBoot(h, root);
    buildIdle(h, root);
    buildProgress(h, root);
    buildVictory(h, root);

    // Start the range (AP + reactor). Challenge One is the active scenario; both flags are still
    // detectable by the progress grid.
    OtService::instance().start(Challenge::One);

    h->timer = lv_timer_create(tick, TICK_MS, h);
    tick(h->timer);
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

    // The OT range (Wi-Fi AP + :80 web + Modbus/sim) is a PERSISTENT system service, started once at
    // boot by the launcher (ChallengeMenu) and shared by every screen. Leaving this app must NOT tear
    // it down -- doing so previously killed the web server AND the Wi-Fi AP for the whole range as soon
    // as the player navigated away from the scoreboard. (Removed: OtService::instance().stop();)
    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const AppManifest manifest = {
    .id = "vanguard.range",
    .name = "OT Range",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::range
