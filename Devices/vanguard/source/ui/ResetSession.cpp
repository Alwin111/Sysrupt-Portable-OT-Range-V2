// SPDX-License-Identifier: Apache-2.0
#include "ResetSession.h"

#include "Challenges.h"
#include "ResetLayout.h"
#include "SystemTheme.h"
#include "Theme.h"

#include <app/event.h>
#include <app/scheduler.h>
#include <app/signal.h>

#include <lvgl.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <csignal>

// Reference: UI designs/reset_page.png. The pixel-accurate layout lives in ResetLayout.cpp
// (buildResetScreen) so the host harness renders the exact same code; this file owns the app
// plumbing and the reset action. Opening the screen resets nothing.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;

void closeSelf() { app_signal_send(app_scheduler_current_app_id(), SIGTERM); }

// resetSession() stops/starts services (blocking ~400 ms each), so it must not run on the UI task.
void resetWorker(void*) {
    resetSession();
    vTaskDelete(nullptr);
}

void onConfirm(lv_event_t*) {
    // Kick the heavy reset onto a worker task, then return to the menu immediately; the list/menu
    // refresh will show the cleared state as the services come back up.
    xTaskCreate(resetWorker, "vgreset", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    closeSelf();
}

void onCancel(lv_event_t*) { closeSelf(); } // nothing is changed

void createWidgets(lv_obj_t* parent, void*) {
    const int cur = selectedChallenge();
    const ChallengeDef& def = challengeByNumber(cur);
    const bool offensive = def.category == Category::Offensive;
    th::installSystemTheme(lv_obj_get_display(parent));

    ResetButtons btns = buildResetScreen(parent, offensive, displayTitle(def),
                                         difficultyLabel(def.difficulty), def.points,
                                         netScore(), SCORE_MAX);

    lv_obj_add_event_cb(btns.reset, onConfirm, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(btns.cancel, onCancel, LV_EVENT_SHORT_CLICKED, nullptr);

    // Default the focus to CANCEL (the safe choice), so a stray SELECT does not reset.
    auto* grp = lv_group_get_default();
    if (grp) {
        lv_obj_update_layout(parent);
        lv_group_focus_obj(btns.cancel);
    }
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

extern const AppManifest reset_manifest = {
    .id = "vanguard.reset",
    .name = "Reset Session",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::ui
