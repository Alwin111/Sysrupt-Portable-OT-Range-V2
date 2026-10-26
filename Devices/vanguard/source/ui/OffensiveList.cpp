// SPDX-License-Identifier: Apache-2.0
#include "OffensiveList.h"

#include "ChallengeListView.h"
#include "Challenges.h"
#include "Theme.h"

#include <app/event.h>
#include <app/scheduler.h>

#include <lvgl.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

// The Offensive challenge list: the shared list screen, in red, over the seven offensive challenges.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;

void createWidgets(lv_obj_t* parent, void*) {
    buildChallengeList(parent, CHALLENGES, CHALLENGE_COUNT, th::offensive(), "OFFENSIVE");
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

extern const AppManifest offensive_manifest = {
    .id = "vanguard.offensive",
    .name = "Offensive",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::ui
