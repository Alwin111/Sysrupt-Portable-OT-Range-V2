// SPDX-License-Identifier: Apache-2.0
#include "Buzzer.h"
#include "ScreenCapture.h"
#include "ot/ui/HmiApp.h"
#include "ot/ui/RangeHub.h"
#include "ui/ChallengeComplete.h"
#include "ui/ChallengeMenu.h"
#include "ui/ChallengeView.h"
#include "ui/Challenges.h"
#include "ui/DefensiveList.h"
#include "ui/Launcher.h"
#include "ui/OffensiveList.h"
#include "ui/ResetSession.h"
#include "ui/SystemTheme.h"
#include "ui/Theme.h"

#include <app/manager.h>
#include <app/signal.h>
#include <app/start.h>

#include <lvgl.h>
#include <lvgl/devices/keyboard.h>
#include <lvgl/lvgl.h>

#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_heap_caps.h>
#include <esp_system.h>

#include <lwip/sockets.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>

// Vanguard controls, matching the original Vanguard firmware:
// - The joystick moves through the UI in the direction it's pushed, OK/SELECT confirm. These come
//   from the "keys" device, a regular keypad read by LVGL (see bi0s,vanguard.dts).
// - BACK leaves the current context and DISP opens Reset Session. These come from the "system-keys"
//   device, which only delivers events to subscribers: they never reach LVGL or text input, and
//   they keep working when an app takes over the keyboard (like Terminal).

constexpr auto* TAG = "Sysrupt";

constexpr auto* KEYPAD_DEVICE_NAME = "keys";
constexpr auto* SYSTEM_KEYS_DEVICE_NAME = "system-keys";

// Must match the system-keys key-codes in bi0s,vanguard.dts
constexpr uint32_t KEY_BACK = CODEPOINT_ESCAPE;
constexpr uint32_t KEY_PAUSE = 0xE000;
constexpr uint32_t KEY_DISP = 0xE001;
constexpr uint32_t KEY_START = 0xE002; // intentionally unassigned (Reset Session now opens via DISP)

constexpr auto POLL_INTERVAL_MS = 20;
constexpr auto LVGL_CHECK_INTERVAL_MS = 500;

// Low-heap self-recovery backstop. This board has no PSRAM, so all heap is scarce internal RAM.
// If a leak or a burst of parallel connections drives free internal heap below the floor and it
// stays there, LWIP can no longer answer ARP/TCP and the badge becomes joinable-but-dead until it
// is manually power-cycled. The controls_task loop watches free heap and, once it has been under
// the floor continuously for the grace period (i.e. the stack is genuinely wedged, not a brief
// legitimate spike), reboots so connectivity recovers on its own. The floor sits well below healthy
// idle heap and the grace is long, so this never fires during normal operation.
constexpr size_t HEAP_FLOOR_BYTES = 4096;
constexpr auto HEAP_CHECK_INTERVAL_MS = 3000;
constexpr auto HEAP_LOW_GRACE_MS = 30000;

// System-level Challenge Complete watcher: poll every challenge's live solved state and pop the one
// shared completion screen on a NEW solve, from whatever LCD screen is up. The baseline is captured a
// few seconds after boot so already-solved (persisted) challenges never re-pop on start-up.
constexpr auto COMPLETE_CHECK_MS = 500;
constexpr auto COMPLETE_WATCH_DELAY_MS = 4000;

// Bring the badge's network services (Wi-Fi AP + :80 web HMI + Modbus + the protocol/defensive
// backends) up a few seconds after boot, from the controls task, so the range is attackable from
// ANY LCD screen -- not only after the player happens to open the challenge menu. The delay lets
// Tactility finish initialising its Wi-Fi radio before RangeNetwork tries to acquire the AP role.
constexpr auto SERVICE_AUTOSTART_DELAY_MS = 3000;

// Network-stack liveness watchdog: the low-heap watchdog only catches SUSTAINED low heap, but a flood
// of parallel connections can exhaust the LWIP pbuf/socket pools and wedge the whole TCP/IP stack
// while general heap recovers -- the badge then looks dead (even ICMP) and never comes back. Detect
// that by self-connecting to the local HTTP server; after a few consecutive failures, self-reboot.
constexpr auto NET_CHECK_MS = 5000;
constexpr int NET_FAIL_REBOOT = 3; // ~15 s of an unresponsive stack -> recover
constexpr auto LVGL_LOCK_TIMEOUT_MS = 200;
constexpr auto DISP_PENDING_TIMEOUT_MS = 5000;

// region System keys

// BACK and DISP must also work while LVGL is stopped: an app can stop it and draw to the display
// itself (e.g. Terminal on boards without PSRAM). So the actions use the app manager directly,
// which is thread-safe, and only the on-screen keyboard handling and starting Settings need LVGL.

static bool get_topmost_app_id(char* buffer, size_t buffer_size) {
    return app_manager_get_topmost_app_id(buffer, buffer_size) == ERROR_NONE;
}

// Asks the foreground app to quit. SIGTERM arrives as APP_EVENT_CLOSE for apps with an event loop
// (like the close button of a toolbar), and terminates POSIX-style apps such as the "shell" child
// of Terminal, which then ends Terminal too.
static bool close_foreground_app() {
    char app_id[64];
    AppInstanceId instance_id = 0;
    if (
        !get_topmost_app_id(app_id, sizeof(app_id)) ||
        strcmp(app_id, CONFIG_TT_LAUNCHER_APP_ID) == 0 ||
        app_manager_get_topmost_instance_id(&instance_id) != ERROR_NONE
    ) {
        return false; // Nothing to go back to
    }
    LOG_I(TAG, "BACK: closing %s", app_id);
    app_signal_send(instance_id, SIGTERM);
    return true;
}

// Caller holds the LVGL lock. Closes the on-screen keyboard if it's showing.
static bool close_software_keyboard() {
    auto* keyboard = lvgl_software_keyboard_get_last();
    if (keyboard == nullptr || keyboard->object == nullptr || lv_obj_has_flag(keyboard->object, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    lvgl_software_keyboard_hide(keyboard);
    return true;
}

// ---- DISP -> Reset Session (in-place modal) state. See build_reset_modal() below for the rationale.
static lv_indev_t* g_keypad_indev = nullptr;        // set by install_keypad_wrapper()
static lv_obj_t* g_reset_modal = nullptr;           // modal on lv_layer_top() (nullptr = hidden)
static lv_group_t* g_reset_group = nullptr;         // input group owning the modal's two buttons
static lv_group_t* g_reset_prev_group = nullptr;    // the app's group, restored when the modal closes
static std::atomic<bool> g_reset_requested{false};  // controls_task performs the reset when set
static void close_reset_modal();

static void on_back(void*) {
    if (g_reset_modal != nullptr) {
        close_reset_modal(); // BACK dismisses the Reset Session modal (same as CANCEL)
        return;
    }
    if (close_software_keyboard()) {
        LOG_I(TAG, "BACK: closed on-screen keyboard");
        return;
    }
    close_foreground_app();
}

// ---- DISP -> Reset Session (in-place modal) ---------------------------------------------------
// The old design opened a separate "vanguard.reset" app, but that app needs a ~16 KB task stack and
// this board (no PSRAM) often has only a few KB of internal heap free, so the app could never start
// and DISP silently did nothing. Instead we draw the confirmation as a small modal on the display's
// top layer, in the LVGL task that is already running - no new task, almost no heap. The actual
// reset (which stops/starts services) is handed to controls_task (its stack is generous), never run
// on the LVGL task. Opening the modal changes nothing; only RESET resets. (State is declared above,
// before on_back, so BACK can also dismiss the modal.)

static void close_reset_modal() {
    if (g_reset_modal == nullptr) {
        return;
    }
    if (g_keypad_indev != nullptr) {
        lv_indev_set_group(g_keypad_indev, g_reset_prev_group);
    }
    lv_obj_t* m = g_reset_modal;
    g_reset_modal = nullptr;
    lv_obj_delete(m);
    if (g_reset_group != nullptr) {
        lv_group_delete(g_reset_group);
        g_reset_group = nullptr;
    }
}

static void on_reset_confirm(lv_event_t*) {
    g_reset_requested.store(true); // controls_task picks this up and runs resetSession()
    close_reset_modal();
    LOG_I(TAG, "DISP/Reset: confirmed - clearing session");
}
static void on_reset_cancel(lv_event_t*) { close_reset_modal(); }

static lv_obj_t* make_reset_btn(lv_obj_t* parent, const char* text, lv_color_t color, lv_event_cb_t cb) {
    namespace th = vanguard::theme;
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_height(b, 38);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, th::surfaceRaised(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, color, 0);
    lv_obj_set_style_border_color(b, th::green(), LV_STATE_FOCUSED); // focus = green, like the rest of the UI
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_SHORT_CLICKED, nullptr);
    return b;
}

static void build_reset_modal() {
    namespace th = vanguard::theme;
    lv_obj_t* scr = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr, th::background(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 18, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 12, 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "RESET SESSION");
    lv_obj_set_style_text_color(title, th::red(), 0);

    lv_obj_t* warn = lv_label_create(scr);
    lv_label_set_text(warn, "Clears ALL challenge progress\nand score. This cannot be undone.");
    lv_obj_set_style_text_color(warn, th::text(), 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);

    lv_obj_t* cancel = make_reset_btn(row, "CANCEL", th::text(), on_reset_cancel);
    lv_obj_t* reset = make_reset_btn(row, "RESET", th::red(), on_reset_confirm);

    g_reset_group = lv_group_create();
    lv_group_add_obj(g_reset_group, cancel);
    lv_group_add_obj(g_reset_group, reset);
    if (g_keypad_indev != nullptr) {
        g_reset_prev_group = lv_indev_get_group(g_keypad_indev);
        lv_indev_set_group(g_keypad_indev, g_reset_group);
    }
    lv_group_focus_obj(cancel); // default focus = CANCEL (safe), so a stray SELECT does not reset
    g_reset_modal = scr;
}

// Runs on the LVGL task. DISP toggles the Reset Session confirmation modal. Opening it changes
// nothing; only its RESET action resets. A second DISP (or BACK / CANCEL) dismisses it.
static void on_disp(void*) {
    if (g_reset_modal != nullptr) {
        close_reset_modal(); // toggle off
        return;
    }
    close_software_keyboard();
    LOG_I(TAG, "DISP: opening Reset Session");
    build_reset_modal();
}

// Runs on the LVGL task. PAUSE is otherwise unassigned, so it's the developer screen capture key.
static void on_pause(void*) {
    vanguard::screencapture::capture(lv_display_get_default());
}

static bool run_on_lvgl(lv_async_cb_t action) {
    if (!lvgl_is_running() || !lvgl_try_lock(pdMS_TO_TICKS(LVGL_LOCK_TIMEOUT_MS))) {
        return false;
    }
    lv_async_call(action, nullptr);
    lvgl_unlock();
    return true;
}

// DISP pressed while LVGL was stopped: Settings is opened once LVGL runs again
static TickType_t disp_pending_since = 0;
static bool disp_pending = false;

static void handle_system_key(uint32_t key) {
    switch (key) {
        case KEY_BACK:
            if (!run_on_lvgl(on_back)) {
                close_foreground_app();
            }
            break;
        case KEY_DISP:
            if (!run_on_lvgl(on_disp)) {
                // The app that stopped LVGL must quit first, which restarts it
                close_foreground_app();
                disp_pending = true;
                disp_pending_since = xTaskGetTickCount();
            }
            break;
        case KEY_PAUSE:
            run_on_lvgl(on_pause);
            break;
        // KEY_START is intentionally unassigned: Reset Session now opens from DISP (see on_disp).
        default:
            break;
    }
}

static void process_pending_disp() {
    if (!disp_pending) {
        return;
    }
    if (xTaskGetTickCount() - disp_pending_since > pdMS_TO_TICKS(DISP_PENDING_TIMEOUT_MS)) {
        LOG_W(TAG, "DISP: LVGL didn't restart in time, not opening Settings");
        disp_pending = false;
        return;
    }
    // Wait for the launcher to be back on top, so Settings opens over it rather than the closing app
    char app_id[64];
    if (lvgl_is_running() && get_topmost_app_id(app_id, sizeof(app_id)) && strcmp(app_id, CONFIG_TT_LAUNCHER_APP_ID) == 0) {
        if (run_on_lvgl(on_disp)) {
            disp_pending = false;
        }
    }
}

// endregion

// region Joystick (LVGL keypad read callback wrapper, runs on the LVGL task)

static lv_indev_read_cb_t original_read_cb = nullptr;

// Widgets that use LEFT/RIGHT themselves (to change a value or move a cursor)
static bool uses_left_right(lv_obj_t* obj) {
    return lv_obj_has_class(obj, &lv_slider_class) ||
        lv_obj_has_class(obj, &lv_roller_class) ||
        lv_obj_has_class(obj, &lv_arc_class) ||
        lv_obj_has_class(obj, &lv_textarea_class) ||
        lv_obj_has_class(obj, &lv_buttonmatrix_class);
}

// The joystick should move through the UI in the direction it's pushed. LVGL only moves focus on
// PREV/NEXT (UP/DOWN send those, see codepoint_to_lv_key()), so LEFT/RIGHT do nothing on most
// screens, including the launcher's horizontal row of buttons. LEFT/RIGHT therefore move focus
// too, unless the focused widget uses them. An open dropdown list gets real UP/DOWN keys instead,
// so its options can be chosen.
static void map_joystick(lv_indev_t* indev, lv_indev_data_t* data) {
    lv_group_t* group = lv_indev_get_group(indev);
    lv_obj_t* focused = group != nullptr ? lv_group_get_focused(group) : nullptr;
    if (focused == nullptr) {
        return;
    }
    if (data->key == LV_KEY_LEFT || data->key == LV_KEY_RIGHT) {
        if (!uses_left_right(focused)) {
            data->key = data->key == LV_KEY_LEFT ? LV_KEY_PREV : LV_KEY_NEXT;
        }
    } else if (data->key == LV_KEY_PREV || data->key == LV_KEY_NEXT) {
        if (lv_obj_has_class(focused, &lv_dropdown_class) && lv_dropdown_is_open(focused)) {
            data->key = data->key == LV_KEY_PREV ? LV_KEY_UP : LV_KEY_DOWN;
        }
    }
}

static void keypad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    original_read_cb(indev, data);
    // Short audible click on each NEW nav-key press (up/down/left/right/select). The read
    // callback is polled repeatedly while a key is held, so beep only on the released->pressed
    // edge to get one click per press, not a continuous tone.
    static lv_indev_state_t prev_state = LV_INDEV_STATE_RELEASED;
    if (data->state == LV_INDEV_STATE_PRESSED && prev_state == LV_INDEV_STATE_RELEASED) {
        vanguard::buzzer::navClick();
    }
    prev_state = data->state;
    map_joystick(indev, data);
}

// Installs the wrapper once LVGL has bound the keypad, and again whenever LVGL was restarted
// (which recreates the input device with the original read callback). Caller holds the LVGL lock.
static void install_keypad_wrapper(Device* keypad) {
    auto* indev = lvgl_keyboard_find_by_device(keypad);
    if (indev == nullptr) {
        return;
    }
    g_keypad_indev = indev; // remembered so the DISP reset modal can grab/restore the input group
    auto read_cb = lv_indev_get_read_cb(indev);
    if (read_cb != keypad_read_cb) {
        original_read_cb = read_cb;
        lv_indev_set_read_cb(indev, keypad_read_cb);
        LOG_I(TAG, "Controls enabled (joystick = navigate, BACK = back, DISP = reset session)");
    }
}

// endregion

static void controls_task(void*) {
    Device* keypad = nullptr;
    Device* system_keys = nullptr;
    KeyboardEventSubscription subscription = {};
    bool subscribed = false;
    TickType_t last_lvgl_check = 0;
    lv_display_t* themed_display = nullptr;
    TickType_t last_heap_check = 0;
    TickType_t heap_low_since = 0;
    bool heap_low = false;
    TickType_t last_complete_check = 0;
    uint32_t seen_complete_mask = 0;
    bool complete_watch_init = false;
    TickType_t last_net_check = 0;
    int net_fail_count = 0;
    bool services_started = false;

    // Navigation buzzer: a short click on each joystick/select press (see Buzzer.h, GPIO7).
    vanguard::buzzer::init();

    while (true) {
        if (keypad == nullptr && device_get_by_name(KEYPAD_DEVICE_NAME, &keypad) != ERROR_NONE) {
            keypad = nullptr;
        }
        if (system_keys == nullptr && device_get_by_name(SYSTEM_KEYS_DEVICE_NAME, &system_keys) != ERROR_NONE) {
            system_keys = nullptr;
        }

        if (!subscribed && system_keys != nullptr && device_is_ready(system_keys)) {
            subscribed = keyboard_subscribe(system_keys, &subscription) == ERROR_NONE;
        }
        if (subscribed) {
            KeyboardKeyData data;
            while (keyboard_poll(system_keys, &subscription, &data) == ERROR_NONE) {
                if (data.pressed) {
                    handle_system_key(data.key);
                }
            }
        }
        process_pending_disp();

        // Load the persisted session (completed challenges + hint usage) once /data is mounted. Done
        // here, on this task's generous stack, so the one-time file read never runs deep inside an
        // LVGL render on the smaller UI task stack. No-op after the first successful load.
        ot::ui::sessionPreload();

        // Perform a confirmed DISP -> Reset Session here, on this task's generous stack (resetSession
        // stops/starts services and must not run on the LVGL task). The modal only sets the flag.
        if (g_reset_requested.exchange(false)) {
            LOG_I(TAG, "Reset Session: clearing progress + reseeding services");
            ot::ui::resetSession();
        }

        // The theme must be in place before apps build their widgets, so this is checked on every
        // pass: it's installed as soon as LVGL runs, and again after LVGL was restarted.
        if (lvgl_is_running()) {
            lv_display_t* display = lv_display_get_default();
            if (display != themed_display && lvgl_try_lock(pdMS_TO_TICKS(LVGL_LOCK_TIMEOUT_MS))) {
                display = lv_display_get_default();
                vanguard::theme::installSystemTheme(display);
                themed_display = display;
                lvgl_unlock();
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (keypad != nullptr && now - last_lvgl_check >= pdMS_TO_TICKS(LVGL_CHECK_INTERVAL_MS)) {
            last_lvgl_check = now;
            if (lvgl_is_running() && lvgl_try_lock(pdMS_TO_TICKS(100))) {
                install_keypad_wrapper(keypad);
                lvgl_unlock();
            }
        }

        // One-time network bring-up: start the Wi-Fi AP + web/Modbus/protocol services here, from the
        // controls task, once Tactility has settled (LVGL running + a short delay so the Wi-Fi radio is
        // initialised). This makes the range joinable and the web HMI reachable from EVERY LCD screen,
        // not only after the player opens the challenge menu -- and it comes back on its own after a
        // self-reboot. startAllServices() is race-safe and idempotent, so the menu calling it too is fine.
        if (!services_started && lvgl_is_running() && now >= pdMS_TO_TICKS(SERVICE_AUTOSTART_DELAY_MS)) {
            services_started = true;
            ot::ui::startAllServices();
            LOG_I(TAG, "Network services auto-started (AP + web HMI up for all screens)");
        }

        // Low-heap safety net (see HEAP_FLOOR_BYTES). Runs on this task's generous stack so it keeps
        // working even if the LVGL/UI task is starved. Self-reboots only after heap has been under
        // the floor continuously for the grace period, so a dead TCP/IP stack recovers on its own
        // instead of hanging until a manual power-cycle.
        if (now - last_heap_check >= pdMS_TO_TICKS(HEAP_CHECK_INTERVAL_MS)) {
            last_heap_check = now;
            size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (free_internal < HEAP_FLOOR_BYTES) {
                if (!heap_low) {
                    heap_low = true;
                    heap_low_since = now;
                    LOG_W(TAG, "Low heap: %u B free (floor %u B)", (unsigned)free_internal, (unsigned)HEAP_FLOOR_BYTES);
                } else if (now - heap_low_since >= pdMS_TO_TICKS(HEAP_LOW_GRACE_MS)) {
                    LOG_E(TAG, "Heap exhausted (%u B) for %d ms - self-rebooting to restore connectivity",
                          (unsigned)free_internal, (int)HEAP_LOW_GRACE_MS);
                    vTaskDelay(pdMS_TO_TICKS(100)); // let the log line flush before the reset
                    esp_restart();
                }
            } else if (heap_low) {
                heap_low = false;
                LOG_I(TAG, "Heap recovered: %u B free", (unsigned)free_internal);
            }
        }

        // ---- System-level Challenge Complete watcher -----------------------------------------------
        // A challenge is usually solved over the network while the LCD is on the menu/list, not that
        // challenge's own view. Poll every challenge's live solved state here (works from ANY screen)
        // and, on a NEW completion, record it and post the number; the challenge view then shows the
        // one shared Challenge Complete screen (opening the view if it is not already foreground).
        if (now - last_complete_check >= pdMS_TO_TICKS(COMPLETE_CHECK_MS)) {
            last_complete_check = now;
            const uint32_t mask = ot::ui::completedLiveMask();
            if (!complete_watch_init) {
                // Capture the baseline only after boot settles (sessionPreload has restored the saved
                // completions), so solved-and-persisted challenges do not re-pop the screen on boot.
                if (now >= pdMS_TO_TICKS(COMPLETE_WATCH_DELAY_MS)) {
                    seen_complete_mask = mask;
                    complete_watch_init = true;
                }
            } else if (mask != seen_complete_mask) {
                const uint32_t newly = mask & ~seen_complete_mask;
                seen_complete_mask = mask;
                int n = 0;
                for (int b = 1; b <= 12; b++) {
                    if (newly & (1u << b)) { n = b; break; }
                }
                if (n > 0) {
                    // Record the completion and signal it. The challenge's own view renders the shared
                    // Challenge Complete screen IN PLACE -- when that view is open, or the next time the
                    // player opens that challenge. We deliberately do NOT launch a second app here: on
                    // this no-PSRAM device, spinning up another LVGL app while the 9 services run drives
                    // free heap to ~0 and wedges the TCP/IP stack (the "crashes"). In-place swap reuses
                    // the existing window, so it stays memory-safe.
                    ot::ui::sessionMarkComplete(n); // idempotent persist
                    ot::ui::postCompletion(n);      // the challenge view shows the complete screen
                }
            }
        }

        // ---- Network-stack liveness watchdog (self-heal a pool-exhaustion wedge) --------------------
        // Self-connect to the local HTTP server. A healthy stack accepts instantly; a wedged stack
        // (pbuf/socket pools exhausted by a flood) fails. After NET_FAIL_REBOOT consecutive failures
        // the stack is not recovering on its own, so reboot -- turning a permanent "dead" into a brief
        // auto-recovery. lru_purge on the :80 server means a self-connect still succeeds while busy.
        if (now - last_net_check >= pdMS_TO_TICKS(NET_CHECK_MS)) {
            last_net_check = now;
            bool net_ok = false;
            int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
            if (fd >= 0) {
                struct timeval tv = { 2, 0 };
                lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                struct sockaddr_in addr = {};
                addr.sin_family = AF_INET;
                addr.sin_port = lwip_htons(80);
                addr.sin_addr.s_addr = lwip_htonl(0x7F000001); // 127.0.0.1 -> the local :80 server
                net_ok = (lwip_connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
                lwip_close(fd);
            }
            if (net_ok) {
                net_fail_count = 0;
            } else if (++net_fail_count >= NET_FAIL_REBOOT) {
                LOG_E(TAG, "TCP/IP stack unresponsive (%d checks) - self-rebooting to recover", net_fail_count);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

extern "C" {

static error_t start() {
    // Registered here, at kernel init, so it exists before Boot looks up the launcher. The app
    // registry doesn't depend on app-module having started yet (see app_manager_add()).
    if (app_manager_add(&vanguard::launcher::manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the Sysrupt launcher");
    }
    if (app_manager_add(&ot::ui::manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the OT Lab app");
    }
    if (app_manager_add(&ot::range::manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the OT Range hub");
    }
    if (app_manager_add(&ot::ui::menu_manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the challenge menu");
    }
    if (app_manager_add(&ot::ui::challenge_manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the challenge view");
    }
    if (app_manager_add(&ot::ui::offensive_manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the offensive list");
    }
    if (app_manager_add(&ot::ui::defensive_manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the defensive list");
    }
    if (app_manager_add(&ot::ui::complete_manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the challenge-complete screen");
    }
    if (app_manager_add(&ot::ui::reset_manifest) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register the reset-session screen");
    }
    if (xTaskCreate(controls_task, "vanguard_ctrl", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        LOG_E(TAG, "Failed to start controls task");
        return ERROR_OUT_OF_MEMORY;
    }
    return ERROR_NONE;
}

Module vanguard_module = {
    .name = "vanguard",
    .start = start,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
