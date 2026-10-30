// SPDX-License-Identifier: Apache-2.0
#include "ChallengeMenu.h"
#include "Challenges.h"
#include "RefUi.h"
#include "SystemTheme.h"
#include "Theme.h"
#include "assets/LogoImg.h"

#include "../ot/Iec104Service.h"
#include "../ot/OpcUaService.h"
#include "../ot/OtService.h"
#include "../BadgeId.h"
#include "../ot/PumpService.h"
#include "../ot/DamService.h"
#include "../ot/DefService.h"
#include "../ot/GooseService.h"
#include "../ot/SnmpService.h"
#include "../ot/SisService.h"
#include "../ot/sim/PlantModel.h"

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <cstdio>
#include <cstring>

// Sysrupt OT Range V2 launcher: the "starting" (join-Wi-Fi) screen, then the Offensive/Defensive
// "category-select" screen. Offensive opens the offensive-list app; Defensive is a visual placeholder
// for now. Matches the supplied reference screens (starting.png, category-select.png). The real
// network is kept (SSID "sysrupt ot", http://10.0.1.1) and shown on the starting screen.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;
namespace rf = ot::ui::ref;

constexpr uint32_t REFRESH_MS = 800;
constexpr auto* URL = "http://10.0.1.1";

struct Menu {
    lv_obj_t* welcome = nullptr;
    lv_obj_t* welcomeWait = nullptr;
    lv_obj_t* category = nullptr;
    lv_obj_t* scoreVal = nullptr;
    lv_obj_t* statusVal = nullptr;
    lv_obj_t* progress = nullptr;
    lv_obj_t* offCard = nullptr;
    lv_obj_t* defCard = nullptr;
    lv_obj_t* completed = nullptr;
    lv_obj_t* overlay = nullptr; // "coming soon" for Defensive
    lv_timer_t* timer = nullptr;
    bool advanced = false;
};

uint8_t clients() { return OtService::instance().networkStatus().clients; }

void buildWelcome(Menu* m, lv_obj_t* parent) {
    auto* s = rf::box(parent);
    m->welcome = s;
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, th::background(), 0);
    lv_obj_set_style_pad_all(s, 5, 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s, 1, 0);

    // top row: BOOTING ... (left) and version (right)
    auto* top = rf::box(s);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    rf::label(top, "BOOTING...", rf::fontSmall(), th::green());
    rf::label(top, "v2.0.0", rf::fontSmall(), th::textMuted());

    // SYSRUPT skyline + OT RANGE V2 + tagline, rendered as the real reference artwork (256x90),
    // sized so the whole starting screen fits the 240px height.
    auto* logo = lv_image_create(s);
    lv_image_set_src(logo, &sysrupt_logo_img);
    lv_obj_set_style_pad_ver(logo, 1, 0);

    // Red-bordered Wi-Fi connection panel (prompt small, SSID big) spanning the full width.
    auto* wifi = rf::panel(s, th::offensive(), LV_PCT(100), 0);
    lv_obj_set_style_margin_top(wifi, 3, 0);
    lv_obj_set_flex_flow(wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi, 1, 0);
    auto* prompt = rf::label(wifi, "JOIN THE WIFI TO START", rf::fontSmall(), th::offensive());
    // Breathing pulse: fade toward ~half opacity and back, never fully out.
    static lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, prompt);
    lv_anim_set_values(&pulse, 110, 255);
    lv_anim_set_duration(&pulse, 850);
    lv_anim_set_playback_duration(&pulse, 850);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&pulse, [](void* o, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(o), (lv_opa_t)v, 0);
    });
    lv_anim_start(&pulse);
    auto* ssidRow = rf::box(wifi);
    lv_obj_set_size(ssidRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ssidRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ssidRow, 6, 0);
    lv_obj_set_flex_align(ssidRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // (No LV_SYMBOL_WIFI icon here: the unscii pixel font has no symbol glyphs, so it rendered as an
    // empty box. The "JOIN THE WIFI TO START" heading + the bordered SSID box already signal Wi-Fi.)
    auto* ssidBox = rf::panel(ssidRow, th::offensive(), 0, 0);
    lv_obj_set_style_pad_ver(ssidBox, 2, 0);
    rf::label(ssidBox, badge::ssid(), rf::fontBig(), th::text());
    rf::label(wifi, "Open your browser and go to:", rf::fontSmall(), th::textMuted());
    rf::label(wifi, URL, rf::fontBig(), th::green());

    m->welcomeWait = rf::label(s, "waiting for Wi-Fi connection...",
                               rf::fontSmall(), th::textMuted());
    lv_obj_set_style_pad_top(m->welcomeWait, 2, 0);
}

// A thin horizontal accent bar that grows to fill the available width (a divider / flank rule).
lv_obj_t* rule(lv_obj_t* parent, lv_color_t accent, bool grow) {
    auto* r = rf::box(parent);
    lv_obj_set_height(r, 2);
    if (grow) lv_obj_set_flex_grow(r, 1); else lv_obj_set_width(r, LV_PCT(100));
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, accent, 0);
    lv_obj_set_style_radius(r, 1, 0);
    return r;
}

// One big category card (Offensive/Defensive), matching the reference: an index flanked by rules,
// the big accent title, divider rules, the sub-label, and a bordered count box.
lv_obj_t* categoryCard(lv_obj_t* parent, const char* idx, const char* title, const char* sub,
                       const char* count, lv_color_t accent) {
    auto* card = lv_button_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, th::background(), 0);
    lv_obj_set_style_radius(card, 5, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    // Focused card: thick green ring (reference highlights the selected card green).
    lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(card, th::green(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 4, 0);

    // Index row: --- [ 01 ] ---
    auto* idxRow = rf::box(card);
    lv_obj_set_width(idxRow, LV_PCT(100));
    lv_obj_set_height(idxRow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(idxRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idxRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(idxRow, 4, 0);
    rule(idxRow, accent, true);
    rf::label(idxRow, idx, rf::fontSmall(), accent);
    rule(idxRow, accent, true);

    auto* t = rf::label(card, title, rf::fontBig(), accent);
    // Tight negative spacing: keeps the 9-char title clear of the card border and packs the
    // glyphs into the chunky blocky look of the reference pixel font (unscii-16 advance is 16px).
    lv_obj_set_style_text_letter_space(t, -2, 0);
    rule(card, accent, false);
    rf::label(card, sub, rf::fontSmall(), th::textMuted());
    rule(card, accent, false);

    auto* cbox = rf::panel(card, accent, 0, 0);
    lv_obj_set_style_pad_ver(cbox, 2, 0);
    lv_obj_set_style_pad_hor(cbox, 8, 0);
    rf::label(cbox, count, rf::fontSmall(), accent);
    return card;
}

void startApp(const char* id) {
    AppStartContext ctx; AppInstanceId inst = 0;
    if (app_start_context_from_id(id, &ctx) == ERROR_NONE) {
        app_start_with_context(&ctx, &inst);
    }
}

void onOffClicked(lv_event_t*) { startApp("vanguard.offensive"); }
void onDefClicked(lv_event_t*) { startApp("vanguard.defensive"); }

void buildCategory(Menu* m, lv_obj_t* parent) {
    auto* s = rf::box(parent);
    m->category = s;
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, th::background(), 0);
    lv_obj_set_style_pad_all(s, 6, 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s, 4, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);

    m->scoreVal = rf::headerBar(s, netScore(), SCORE_MAX, th::green());

    auto* statusRow = rf::box(s);
    lv_obj_set_size(statusRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(statusRow, 4, 0);
    rf::label(statusRow, "STATUS:", rf::fontSmall(), th::green());
    m->statusVal = rf::label(statusRow, "CONNECTED", rf::fontSmall(), th::text());

    auto* cards = rf::box(s);
    lv_obj_set_width(cards, LV_PCT(100));
    lv_obj_set_flex_grow(cards, 1);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, 6, 0);
    static char offCount[16]; snprintf(offCount, sizeof(offCount), "%02d CHALLENGES", CHALLENGE_COUNT);
    static char defCount[16]; snprintf(defCount, sizeof(defCount), "%02d CHALLENGES", DEFENSIVE_COUNT);
    // Defensive first: it is category [ 01 ] and leads the row (challenges are numbered 01-04
    // Defensive, 05-08 Offensive).
    m->defCard = categoryCard(cards, "[ 01 ]", "DEFENSIVE", "DETECT & PROTECT", defCount, th::defensive());
    m->offCard = categoryCard(cards, "[ 02 ]", "OFFENSIVE", "ATTACK & EXPLOIT", offCount, th::offensive());
    lv_obj_add_event_cb(m->defCard, onDefClicked, LV_EVENT_SHORT_CLICKED, m);
    lv_obj_add_event_cb(m->offCard, onOffClicked, LV_EVENT_SHORT_CLICKED, m);

    // Section divider: --- SELECT A CATEGORY --- (green rules flanking the label).
    auto* selRow = rf::box(s);
    lv_obj_set_width(selRow, LV_PCT(100));
    lv_obj_set_height(selRow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(selRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(selRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(selRow, 6, 0);
    rule(selRow, th::green(), true);
    rf::label(selRow, "SELECT A CATEGORY", rf::fontSmall(), th::green());
    rule(selRow, th::green(), true);

    auto* foot = rf::panel(s, th::green(), LV_PCT(100), 0);
    lv_obj_set_style_pad_ver(foot, 2, 0);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char initProg[40]; snprintf(initProg, sizeof(initProg), "PROGRESS: 0/%d", SCORE_MAX);
    m->progress = rf::label(foot, initProg, rf::fontSmall(), th::green());
    m->completed = rf::label(foot, "0 COMPLETED", rf::fontSmall(), th::green());
}

void refresh(lv_timer_t* timer) {
    auto* m = static_cast<Menu*>(lv_timer_get_user_data(timer));
    const uint8_t c = clients();
    if (!m->advanced) {
        if (m->welcomeWait) {
            if (c > 0) {
                lv_label_set_text(m->welcomeWait, "CONNECTED - entering...");
                lv_obj_set_style_text_color(m->welcomeWait, th::green(), 0);
            }
        }
        if (c > 0) {
            m->advanced = true;
            lv_obj_add_flag(m->welcome, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(m->category, LV_OBJ_FLAG_HIDDEN);
            if (m->defCard) { lv_group_focus_obj(m->defCard); lv_obj_add_state(m->defCard, LV_STATE_FOCUS_KEY); }
        }
        return;
    }
    // category page live updates
    if (m->scoreVal) { char s[16]; snprintf(s, sizeof(s), "%d/%d", netScore(), SCORE_MAX); lv_label_set_text(m->scoreVal, s); }
    if (m->progress) {
        char s[40]; snprintf(s, sizeof(s), "PROGRESS: %d/%d", netScore(), SCORE_MAX);
        lv_label_set_text(m->progress, s);
    }
    if (m->completed) {
        int done = 0;
        for (const auto& ch : CHALLENGES) if (challengeStatus(ch) == ChStatus::Completed) done++;
        char s[20]; snprintf(s, sizeof(s), "%d COMPLETED", done);
        lv_label_set_text(m->completed, s);
    }
    // Persist any completion observed here (e.g. a challenge solved while the menu is on screen),
    // so progress is written to flash even if the player never opens the per-category list view.
    sessionFlushIfDirty();
}

void onRootDeleted(lv_event_t* e) {
    auto* m = static_cast<Menu*>(lv_event_get_user_data(e));
    if (m->timer) lv_timer_delete(m->timer);
    delete m;
}

void createWidgets(lv_obj_t* parent, void*) {
    th::installSystemTheme(lv_obj_get_display(parent));

    // Ensure every badge service is up. The boot controls task already brings these up a few seconds
    // after boot (so the range is attackable from ANY screen, not just this menu); this call is a
    // race-safe no-op if that has already happened, and a backstop if it has not.
    startAllServices();

    auto* m = new Menu();
    auto* root = rf::box(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, th::background(), 0);
    lv_obj_add_event_cb(root, onRootDeleted, LV_EVENT_DELETE, m);

    buildCategory(m, root); // built first (hidden), welcome drawn on top
    buildWelcome(m, root);

    m->timer = lv_timer_create(refresh, REFRESH_MS, m);
    refresh(m->timer);
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

extern const AppManifest menu_manifest = {
    .id = "vanguard.menu",
    .name = "OT Range",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::ui
