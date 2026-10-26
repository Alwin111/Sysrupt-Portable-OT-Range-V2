// SPDX-License-Identifier: Apache-2.0
#include "ChallengeComplete.h"

#include "Buzzer.h"
#include "Challenges.h"
#include "CompleteLayout.h"
#include "SystemTheme.h"
#include "Theme.h"

#include <app/event.h>
#include <app/scheduler.h>
#include <app/signal.h>
#include <app/start.h>

#include <lvgl.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <csignal>

// Reference: UI designs/chall_completionpage.png. The pixel-accurate layout lives in
// CompleteLayout.cpp (buildCompleteScreen) so the host harness renders the exact same code;
// this file owns the app plumbing, real challenge data, and input callbacks.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;

inline lv_color_t blue() { return th::defensive(); }

struct View {
    int number = 0;
    bool hasNext = false;
};

void startApp(const char* id) {
    AppStartContext ctx; AppInstanceId inst = 0;
    if (app_start_context_from_id(id, &ctx) == ERROR_NONE) {
        app_start_with_context(&ctx, &inst);
    }
}

// Clean self-close: SIGTERM arrives as APP_EVENT_CLOSE, so the event loop tears the window down
// properly (unlike app_scheduler_exit_current, which would leak the app's allocations).
void closeSelf() {
    app_signal_send(app_scheduler_current_app_id(), SIGTERM);
}

void onRootDeleted(lv_event_t* e) {
    delete static_cast<View*>(lv_event_get_user_data(e));
}

void onNext(lv_event_t* e) {
    auto* v = static_cast<View*>(lv_event_get_user_data(e));
    const int n = nextChallengeNumber(v->number);
    if (n > 0) {
        setSelectedChallenge(n);
        startApp("vanguard.challenge"); // the next challenge opens on top...
    }
    // ...and we close ourselves either way (no next -> fall back to the challenge list underneath).
    closeSelf();
}

void onList(lv_event_t* e) {
    (void)e;
    closeSelf(); // returns to the challenge list (ChallengeView already closed itself on completion)
}

void createWidgets(lv_obj_t* parent, void*) {
    auto* v = new View();
    v->number = selectedChallenge();
    const ChallengeDef& def = challengeByNumber(v->number);
    const bool offensive = def.category == Category::Offensive;
    v->hasNext = nextChallengeNumber(v->number) > 0;
    th::installSystemTheme(lv_obj_get_display(parent));

    CompleteButtons btns = buildCompleteScreen(parent, offensive, displayTitle(def),
                                               difficultyLabel(def.difficulty), def.points,
                                               netScore(), SCORE_MAX, v->hasNext);

    // Sound the success chime as the "challenge solved" screen appears (mirrors the inctf
    // Vanguard firmware's flag-accepted jingle). Non-blocking, so it never stalls the build-out.
    vanguard::buzzer::playSuccess();

    lv_obj_t* root = lv_obj_get_child(parent, 0);
    if (root) lv_obj_add_event_cb(root, onRootDeleted, LV_EVENT_DELETE, v);
    lv_obj_add_event_cb(btns.next, onNext, LV_EVENT_SHORT_CLICKED, v);
    lv_obj_add_event_cb(btns.list, onList, LV_EVENT_SHORT_CLICKED, v);

    // Focus the primary action so SELECT acts immediately; joystick L/R moves between the two.
    auto* grp = lv_group_get_default();
    if (grp) {
        lv_obj_update_layout(parent);
        lv_group_focus_obj(btns.next);
        lv_obj_add_state(btns.next, LV_STATE_FOCUS_KEY);
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

extern const AppManifest complete_manifest = {
    .id = "vanguard.complete",
    .name = "Challenge Complete",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = 0 }
};

} // namespace ot::ui
