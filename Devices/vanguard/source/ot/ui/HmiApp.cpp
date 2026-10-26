// SPDX-License-Identifier: Apache-2.0
#include "HmiApp.h"
#include "../OtService.h"
#include "../sim/PlantConfig.h"
#include "../sim/Registers.h"
#include "../../ui/Theme.h"

#include <app/event.h>
#include <app/scheduler.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>

#include <cstdio>

namespace ot::ui {

namespace {

namespace th = vanguard::theme;

constexpr uint32_t REFRESH_MS = 200;

struct Gauge {
    lv_obj_t* bar;
    lv_obj_t* value;
};

struct Hmi {
    lv_obj_t* root;
    lv_obj_t* title;
    lv_obj_t* state;
    lv_obj_t* net;
    Gauge flux, temp, pressure, flow;
    Gauge rod, water, valve;
    lv_obj_t* safeTemp;
    lv_obj_t* safePress;
    lv_obj_t* safeFlux;
    lv_obj_t* status;
    lv_timer_t* timer;
    Challenge challenge;
};

lv_obj_t* box(lv_obj_t* parent) {
    auto* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// One labelled bar: "NAME" on the left, a bar, and a value readout.
Gauge gauge(lv_obj_t* parent, const char* name, int32_t range) {
    auto* row = box(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 5, 0);

    auto* name_label = label(row, name, lvgl_get_text_font(FONT_SIZE_SMALL), th::textMuted());
    lv_obj_set_width(name_label, 38);

    auto* bar = lv_bar_create(row);
    lv_obj_set_size(bar, 118, 9);
    lv_obj_set_flex_grow(bar, 0);
    lv_bar_set_range(bar, 0, range);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, th::surfaceRaised(), 0);
    lv_obj_set_style_bg_color(bar, th::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);

    auto* value = label(row, "-", lvgl_get_text_font(FONT_SIZE_SMALL), th::text());
    lv_obj_set_width(value, 58);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);

    return { bar, value };
}

// A small rounded chip used as a safe-band indicator.
lv_obj_t* chip(lv_obj_t* parent, const char* text) {
    auto* c = box(parent);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 7, 0);
    lv_obj_set_style_pad_ver(c, 2, 0);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, th::surfaceRaised(), 0);
    auto* l = label(c, text, lvgl_get_text_font(FONT_SIZE_SMALL), th::textMuted());
    lv_obj_center(l);
    return c;
}

void setChip(lv_obj_t* chip_obj, bool ok) {
    lv_obj_set_style_bg_color(chip_obj, ok ? th::green() : th::surfaceRaised(), 0);
    auto* l = lv_obj_get_child(chip_obj, 0);
    lv_obj_set_style_text_color(l, ok ? th::textOnAccent() : th::textMuted(), 0);
}

void setGauge(const Gauge& g, int32_t barValue, const char* text, bool inBand) {
    lv_bar_set_value(g.bar, barValue, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g.bar, inBand ? th::green() : th::accent(), LV_PART_INDICATOR);
    lv_label_set_text(g.value, text);
}

void getNetText(char* out, size_t cap) {
    // The badge is the range's Wi-Fi AP; show the SSID and how many attackers have joined.
    const auto st = OtService::instance().networkStatus();
    snprintf(out, cap, "AP:%s  clients:%u", OtService::instance().ssid(), (unsigned)st.clients);
}

void refresh(lv_timer_t* timer) {
    auto* hmi = static_cast<Hmi*>(lv_timer_get_user_data(timer));
    const PlantSnapshot s = OtService::instance().snapshot();
    char buf[24];

    // State
    if (s.scram) {
        lv_label_set_text(hmi->state, "SCRAM");
        lv_obj_set_style_text_color(hmi->state, lv_color_hex(0xFF5A5A), 0);
    } else {
        lv_label_set_text(hmi->state, "RUNNING");
        lv_obj_set_style_text_color(hmi->state, th::green(), 0);
    }

    // Sensors
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)(s.neutron_flux / 10));
    setGauge(hmi->flux, s.neutron_flux, buf, s.safe_band & reg::SAFE_FLUX);
    snprintf(buf, sizeof(buf), "%d.%dC", s.core_temp / 10, (s.core_temp % 10 + 10) % 10);
    setGauge(hmi->temp, s.core_temp, buf, s.safe_band & reg::SAFE_TEMP);
    snprintf(buf, sizeof(buf), "%u.%ub", s.pressure / 10, s.pressure % 10);
    setGauge(hmi->pressure, s.pressure, buf, s.safe_band & reg::SAFE_PRESSURE);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)(s.coolant_flow / 10));
    setGauge(hmi->flow, s.coolant_flow, buf, true);

    // Actuators (setpoints; shown in neutral cyan)
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)(s.rod_position / 10));
    setGauge(hmi->rod, s.rod_position, buf, false);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)(s.water_flow / 10));
    setGauge(hmi->water, s.water_flow, buf, false);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)(s.pressure_valve / 10));
    setGauge(hmi->valve, s.pressure_valve, buf, false);

    setChip(hmi->safeTemp, s.safe_band & reg::SAFE_TEMP);
    setChip(hmi->safePress, s.safe_band & reg::SAFE_PRESSURE);
    setChip(hmi->safeFlux, s.safe_band & reg::SAFE_FLUX);

    // Status line
    const bool solved = (hmi->challenge == Challenge::One) ? s.ch1_solved : s.ch2_solved;
    if (solved) {
        lv_label_set_text(hmi->status, "FLAG UNLOCKED - read it over Modbus");
        lv_obj_set_style_text_color(hmi->status, th::green(), 0);
    } else if (hmi->challenge == Challenge::One) {
        const unsigned pct = (unsigned)((uint64_t)s.ch1_hold_ms * 100 / cfg::CH1_HOLD_MS);
        snprintf(buf, sizeof(buf), "SAFE HOLD %u%%", pct > 100 ? 100 : pct);
        lv_label_set_text(hmi->status, buf);
        lv_obj_set_style_text_color(hmi->status, th::textMuted(), 0);
    } else {
        lv_label_set_text(hmi->status, "Defeat the unlock window + revert loop");
        lv_obj_set_style_text_color(hmi->status, th::textMuted(), 0);
    }
}

void buildHmi(lv_obj_t* parent, Challenge challenge);
void buildSelector(lv_obj_t* parent);

void onSelect(lv_event_t* e) {
    auto challenge = (Challenge)(uintptr_t)lv_event_get_user_data(e);
    auto* parent = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target_obj(e)));
    lv_obj_clean(parent);
    OtService::instance().start(challenge);
    buildHmi(parent, challenge);
}

void buildSelector(lv_obj_t* parent) {
    auto* root = box(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, th::background(), 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 12, 0);

    label(root, "OT REACTOR LAB", lvgl_get_text_font(FONT_SIZE_LARGE), th::accent());
    label(root, "Select a challenge", lvgl_get_text_font(FONT_SIZE_SMALL), th::textMuted());

    lv_obj_t* first = nullptr;
    const char* names[] = { "CHALLENGE 1  -  Setpoint Safety", "CHALLENGE 2  -  Unlock TOCTOU" };
    for (int i = 0; i < 2; i++) {
        auto* btn = lv_button_create(root);
        lv_obj_set_width(btn, 240);
        lv_obj_add_event_cb(btn, onSelect, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)(i == 0 ? Challenge::One : Challenge::Two));
        auto* l = lv_label_create(btn);
        lv_label_set_text(l, names[i]);
        lv_obj_center(l);
        if (i == 0) first = btn;
    }
    if (first != nullptr) {
        lv_obj_update_layout(root);
        lv_group_focus_obj(first);
        lv_obj_add_state(first, LV_STATE_FOCUS_KEY);
    }
}

void buildHmi(lv_obj_t* parent, Challenge challenge) {
    auto* hmi = new Hmi();
    hmi->challenge = challenge;

    auto* root = box(parent);
    hmi->root = root;
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, th::background(), 0);
    lv_obj_set_style_pad_all(root, 6, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 3, 0);

    // Header
    auto* header = box(root);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hmi->title = label(header, challenge == Challenge::One ? "REACTOR  CH1" : "REACTOR  CH2",
                       lvgl_get_text_font(FONT_SIZE_DEFAULT), th::accent());
    hmi->state = label(header, "RUNNING", lvgl_get_text_font(FONT_SIZE_SMALL), th::green());

    char net[24];
    getNetText(net, sizeof(net));
    hmi->net = label(root, net, lvgl_get_text_font(FONT_SIZE_SMALL), th::textMuted());

    // Two columns: sensors (left), actuators (right)
    auto* cols = box(root);
    lv_obj_set_size(cols, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cols, 2, 0);
    label(cols, "SENSORS", lvgl_get_text_font(FONT_SIZE_SMALL), th::accentDim());
    hmi->flux = gauge(cols, "FLUX", cfg::PERMILLE_MAX);
    hmi->temp = gauge(cols, "TEMP", cfg::CORE_TEMP_MAX);
    hmi->pressure = gauge(cols, "PRES", cfg::PRESSURE_MAX);
    hmi->flow = gauge(cols, "FLOW", cfg::PERMILLE_MAX);
    label(cols, "ACTUATORS", lvgl_get_text_font(FONT_SIZE_SMALL), th::accentDim());
    hmi->rod = gauge(cols, "ROD", cfg::PERMILLE_MAX);
    hmi->water = gauge(cols, "PUMP", cfg::PERMILLE_MAX);
    hmi->valve = gauge(cols, "VALV", cfg::PERMILLE_MAX);

    // Safe-band chips
    auto* chips = box(root);
    lv_obj_set_size(chips, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chips, 6, 0);
    hmi->safeTemp = chip(chips, "TEMP");
    hmi->safePress = chip(chips, "PRES");
    hmi->safeFlux = chip(chips, "FLUX");

    hmi->status = label(root, "", lvgl_get_text_font(FONT_SIZE_SMALL), th::textMuted());

    hmi->timer = lv_timer_create(refresh, REFRESH_MS, hmi);
    lv_obj_set_user_data(root, hmi);
    lv_obj_add_event_cb(root, [](lv_event_t* e) {
        auto* h = static_cast<Hmi*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
        if (h != nullptr) {
            lv_timer_delete(h->timer);
            delete h;
        }
    }, LV_EVENT_DELETE, nullptr);

    refresh(hmi->timer);
}

void createWidgets(lv_obj_t* parent, void*) {
    buildSelector(parent);
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
            if (event.type == APP_EVENT_CLOSE) {
                close = true;
            }
        }
    }

    // The OT range (Wi-Fi AP + :80 web + Modbus/sim) is a PERSISTENT system service, started once at
    // boot by the launcher (ChallengeMenu) and shared by every screen. Leaving this app must NOT tear
    // it down -- doing so previously killed the web server AND the Wi-Fi AP for the whole range as soon
    // as the player navigated away from the OT Lab. (Removed: OtService::instance().stop();)
    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const AppManifest manifest = {
    .id = "vanguard.ot",
    .name = "OT Lab",
    .category = APP_CATEGORY_USER,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::ui
