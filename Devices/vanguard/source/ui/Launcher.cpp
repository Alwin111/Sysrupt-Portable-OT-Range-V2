// SPDX-License-Identifier: Apache-2.0
#include "Launcher.h"
#include "SystemTheme.h"
#include "Theme.h"

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstdio>

// The Vanguard home screen: a header with the Vanguard wordmark and a live status panel, a row of
// tiles for the main destinations, and a footer describing the focused tile. Navigated with the
// joystick (any direction moves between tiles) and opened with OK/SELECT.
namespace vanguard::launcher {

namespace {

struct Tile {
    const char* appId;
    const char* icon;
    const char* label;
    const char* description;
};

constexpr Tile TILES[] = {
    { "vanguard.ot", LVGL_ICON_SHARED_HUB, "OT LAB", "Reactor HMI - Modbus + S7comm CTF target" },
    { "tactility.applist", LVGL_ICON_SHARED_APPS, "APPS", "Browse and launch installed apps" },
    { "tactility.terminal", LVGL_ICON_SHARED_TERMINAL, "SHELL", "Command line - BACK to exit" },
    { "tactility.files", LVGL_ICON_SHARED_FOLDER, "FILES", "Browse internal storage" },
    { "tactility.settings", LVGL_ICON_SHARED_SETTINGS, "SETTINGS", "WiFi, display and system settings" },
};
constexpr size_t TILE_COUNT = sizeof(TILES) / sizeof(TILES[0]);

constexpr uint32_t STATUS_REFRESH_MS = 2000;

// Remembered across visits: the window's widgets are recreated every time the launcher returns to
// the top, and focus should come back to the tile that was used last.
size_t lastFocusedTile = 0;

struct Widgets {
    lv_obj_t* network = nullptr;
    lv_obj_t* system = nullptr;
    lv_obj_t* description = nullptr;
    lv_timer_t* statusTimer = nullptr;
};

// region Status

void updateNetworkStatus(lv_obj_t* label) {
    Device* wifi = nullptr;
    char text[48] = "WIFI  OFF";
    lv_color_t color = theme::textMuted();
    if (device_get_first_active_by_type(&WIFI_TYPE, &wifi) == ERROR_NONE) {
        WifiStationState state = WIFI_STATION_STATE_DISCONNECTED;
        char ipv4[16] = {};
        if (wifi_get_station_state(wifi, &state) == ERROR_NONE) {
            if (state == WIFI_STATION_STATE_CONNECTED && wifi_station_get_ipv4_address(wifi, ipv4) == ERROR_NONE) {
                snprintf(text, sizeof(text), "%s", ipv4);
                color = theme::green();
            } else if (state == WIFI_STATION_STATE_CONNECTION_PENDING) {
                snprintf(text, sizeof(text), "CONNECTING");
                color = theme::warning();
            } else {
                snprintf(text, sizeof(text), "OFFLINE");
            }
        }
        device_put(wifi);
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_STATE_DEFAULT);
}

void updateSystemStatus(lv_obj_t* label) {
    const uint32_t free_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    const uint32_t uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    lv_label_set_text_fmt(label, "%luK FREE  %02lu:%02lu", free_kb, uptime_s / 3600, (uptime_s / 60) % 60);
}

void onStatusTimer(lv_timer_t* timer) {
    auto* widgets = static_cast<Widgets*>(lv_timer_get_user_data(timer));
    updateNetworkStatus(widgets->network);
    updateSystemStatus(widgets->system);
}

// endregion

// region Widgets

lv_obj_t* createContainer(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* createLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_STATE_DEFAULT);
    return label;
}

void createHeader(lv_obj_t* parent, Widgets* widgets) {
    auto* header = createContainer(parent);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Wordmark with an accent bar
    auto* brand = createContainer(header);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brand, 8, LV_STATE_DEFAULT);

    auto* bar = createContainer(brand);
    lv_obj_set_size(bar, 4, 34);
    lv_obj_set_style_radius(bar, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, theme::accent(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bar, theme::green(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_STATE_DEFAULT);

    auto* titles = createContainer(brand);
    lv_obj_set_size(titles, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
    auto* title = createLabel(titles, "SYSRUPT", lvgl_get_text_font(FONT_SIZE_LARGE), theme::text());
    lv_obj_set_style_text_letter_space(title, 4, LV_STATE_DEFAULT);
    auto* subtitle = createLabel(titles, "OT RANGE V2", lvgl_get_text_font(FONT_SIZE_SMALL), theme::green());
    lv_obj_set_style_text_letter_space(subtitle, 1, LV_STATE_DEFAULT);

    // Live status panel
    auto* status = createContainer(header);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_hor(status, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(status, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(status, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(status, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(status, theme::surface(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(status, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(status, theme::border(), LV_STATE_DEFAULT);

    widgets->network = createLabel(status, "", lvgl_get_text_font(FONT_SIZE_SMALL), theme::textMuted());
    widgets->system = createLabel(status, "", lvgl_get_text_font(FONT_SIZE_SMALL), theme::textMuted());
    updateNetworkStatus(widgets->network);
    updateSystemStatus(widgets->system);
}

// The icon and label take on the tile's focus state, so their LV_STATE_FOCUSED styles apply
void setChildrenFocused(lv_obj_t* tile, bool focused) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(tile); i++) {
        auto* child = lv_obj_get_child(tile, i);
        if (focused) {
            lv_obj_add_state(child, LV_STATE_FOCUSED);
        } else {
            lv_obj_remove_state(child, LV_STATE_FOCUSED);
        }
    }
}

void onTileFocused(lv_event_t* e) {
    auto* widgets = static_cast<Widgets*>(lv_event_get_user_data(e));
    auto* tile = lv_event_get_target_obj(e);
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(tile));
    lastFocusedTile = index;
    setChildrenFocused(tile, true);
    lv_label_set_text(widgets->description, TILES[index].description);
}

void onTileDefocused(lv_event_t* e) {
    setChildrenFocused(lv_event_get_target_obj(e), false);
}

void onTileClicked(lv_event_t* e) {
    auto* tile = lv_event_get_target_obj(e);
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(tile));
    AppStartContext context;
    AppInstanceId instance_id = 0;
    if (app_start_context_from_id(TILES[index].appId, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }
}

lv_obj_t* createTile(lv_obj_t* parent, size_t index, Widgets* widgets) {
    const auto& tile = TILES[index];
    auto* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
    lv_obj_set_size(button, 58, 74);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(button, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(button, theme::RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, theme::surface(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, theme::border(), LV_STATE_DEFAULT);

    // Focused: lifted, green border and glow (green marks the selection everywhere)
    lv_obj_set_style_bg_color(button, theme::selected(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(button, theme::green(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(button, 14, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(button, theme::green(), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(button, LV_OPA_30, LV_STATE_FOCUSED);
    lv_obj_set_style_translate_y(button, -3, LV_STATE_FOCUSED);
    // Pressed: brief flash
    lv_obj_set_style_bg_color(button, theme::greenDim(), LV_STATE_PRESSED);

    auto* icon = createLabel(button, tile.icon, lvgl_get_shared_icon_font_2x(), theme::accent());
    lv_obj_set_style_text_color(icon, theme::green(), LV_STATE_FOCUSED);
    auto* label = createLabel(button, tile.label, lvgl_get_text_font(FONT_SIZE_SMALL), theme::textMuted());
    lv_obj_set_style_text_letter_space(label, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, theme::green(), LV_STATE_FOCUSED);

    lv_obj_add_event_cb(button, onTileFocused, LV_EVENT_FOCUSED, widgets);
    lv_obj_add_event_cb(button, onTileDefocused, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(button, onTileClicked, LV_EVENT_SHORT_CLICKED, widgets);
    return button;
}

void createFooter(lv_obj_t* parent, Widgets* widgets) {
    auto* footer = createContainer(parent);
    lv_obj_set_size(footer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(footer, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(footer, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(footer, theme::border(), LV_STATE_DEFAULT);

    widgets->description = createLabel(footer, "", lvgl_get_text_font(FONT_SIZE_SMALL), theme::text());
    createLabel(footer, "OK OPEN", lvgl_get_text_font(FONT_SIZE_SMALL), theme::green());
}

void onRootDeleted(lv_event_t* e) {
    auto* widgets = static_cast<Widgets*>(lv_event_get_user_data(e));
    if (widgets->statusTimer != nullptr) {
        lv_timer_delete(widgets->statusTimer);
    }
    delete widgets;
}

void createWidgets(lv_obj_t* parent, void*) {
    // The launcher is the first app LVGL shows, so this puts the theme in place before any app
    // builds its widgets (the board module also installs it, as a fallback after LVGL restarts)
    theme::installSystemTheme(lv_obj_get_display(parent));

    auto* widgets = new Widgets();

    auto* root = createContainer(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(root, 1);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(root, theme::background(), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(root, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(root, 10, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(root, onRootDeleted, LV_EVENT_DELETE, widgets);

    createHeader(root, widgets);

    auto* tiles = createContainer(root);
    lv_obj_set_size(tiles, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(tiles, 6, LV_STATE_DEFAULT);

    createFooter(root, widgets);

    lv_obj_t* focusTile = nullptr;
    for (size_t i = 0; i < TILE_COUNT; i++) {
        auto* tile = createTile(tiles, i, widgets);
        if (i == lastFocusedTile) {
            focusTile = tile;
        }
    }

    widgets->statusTimer = lv_timer_create(onStatusTimer, STATUS_REFRESH_MS, widgets);

    // Keypad-only device: start with a tile selected, so the first key press already acts on it
    if (focusTile != nullptr) {
        lv_obj_update_layout(root);
        lv_group_focus_obj(focusTile);
        lv_obj_add_state(focusTile, LV_STATE_FOCUS_KEY);
        setChildrenFocused(focusTile, true);
        lv_label_set_text(widgets->description, TILES[lastFocusedTile].description);
    }
}

// endregion

int32_t appMain(int, char*[]) {
    const AppInstanceId instance_id = app_scheduler_current_app_id();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription subscription {};
    check(app_event_subscribe(&subscription, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(instance_id, createWidgets, nullptr);

    // The home screen stays resident until the scheduler asks it to close
    bool close = false;
    while (!close) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);
        AppEvent event {};
        while (app_event_poll(&subscription, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                close = true;
            }
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&subscription) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const AppManifest manifest = {
    .id = "vanguard.launcher",
    .name = "Sysrupt",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 3072, .desired_memory_capability = 0 }
};

} // namespace vanguard::launcher
