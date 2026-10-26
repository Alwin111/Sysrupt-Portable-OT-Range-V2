// SPDX-License-Identifier: Apache-2.0
#include <drivers/gpio_keys.h>

#include <gpio_keys_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdlib>
#include <new>

#define TAG "GpioKeys"
#define GET_CONFIG(device) (static_cast<const GpioKeysConfig*>((device)->config))

constexpr auto GPIO_KEYS_MAX = 16;
// Worst case: every key changes state within the same polling interval.
constexpr auto GPIO_KEYS_PENDING_CAPACITY = GPIO_KEYS_MAX;
// Scan interval of the emit-only polling task: well below the debounce time, and comparable to
// how often LVGL polls a regular keypad.
constexpr auto GPIO_KEYS_EMIT_POLL_MS = 10;

struct GpioKeysPendingEvent {
    uint32_t key;
    bool pressed;
};

struct GpioKeysKeyState {
    GpioDescriptor* descriptor;
    bool debounced_pressed;
    bool last_raw_pressed;
    uint32_t last_raw_change_time;
};

struct GpioKeysInternal {
    uint8_t key_count = 0;
    GpioKeysKeyState keys[GPIO_KEYS_MAX] = {};
    GpioKeysPendingEvent pending[GPIO_KEYS_PENDING_CAPACITY] = {};
    uint8_t pending_head = 0;
    uint8_t pending_count = 0;
    // emit-only mode
    TaskHandle_t emit_task = nullptr;
    std::atomic<bool> emit_running {false};
    std::atomic<bool> emit_exited {false};
};

static void push_pending(GpioKeysInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= GPIO_KEYS_PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % GPIO_KEYS_PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
}

static bool pop_pending(GpioKeysInternal* internal, GpioKeysPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % GPIO_KEYS_PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// Debounces every key and queues an event for each debounced level change
static void poll_keys(const GpioKeysConfig* config, GpioKeysInternal* internal) {
    uint32_t now = get_millis();
    for (uint8_t i = 0; i < internal->key_count; i++) {
        auto& state = internal->keys[i];
        bool raw_pressed = false;
        if (gpio_descriptor_get_level(state.descriptor, &raw_pressed) != ERROR_NONE) {
            continue;
        }

        if (raw_pressed != state.last_raw_pressed) {
            state.last_raw_pressed = raw_pressed;
            state.last_raw_change_time = now;
            continue;
        }

        if (raw_pressed == state.debounced_pressed || (now - state.last_raw_change_time) < config->debounce_ms) {
            continue;
        }

        state.debounced_pressed = raw_pressed;
        LOG_I(TAG, "GPIO %u %s (code 0x%04X)", (unsigned)config->pins[i].pin, raw_pressed ? "pressed" : "released", (unsigned)config->key_codes[i]);
        push_pending(internal, config->key_codes[i], raw_pressed);
    }
}

// region emit-only mode

static void emit_task_main(void* context) {
    auto* device = static_cast<Device*>(context);
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<GpioKeysInternal*>(device_get_driver_data(device));

    while (internal->emit_running.load()) {
        poll_keys(config, internal);
        GpioKeysPendingEvent event;
        while (pop_pending(internal, &event)) {
            KeyboardKeyData data = {};
            data.key = event.key;
            data.pressed = event.pressed;
            keyboard_emit_key(device, data);
        }
        vTaskDelay(pdMS_TO_TICKS(GPIO_KEYS_EMIT_POLL_MS));
    }

    internal->emit_exited.store(true);
    vTaskDelete(nullptr);
}

static error_t start_emit_task(Device* device, GpioKeysInternal* internal) {
    internal->emit_running.store(true);
    if (xTaskCreate(emit_task_main, "gpio_keys", 3072, device, tskIDLE_PRIORITY + 2, &internal->emit_task) != pdPASS) {
        internal->emit_running.store(false);
        return ERROR_OUT_OF_MEMORY;
    }
    return ERROR_NONE;
}

static void stop_emit_task(GpioKeysInternal* internal) {
    if (internal->emit_task == nullptr) {
        return;
    }
    internal->emit_running.store(false);
    while (!internal->emit_exited.load()) {
        vTaskDelay(pdMS_TO_TICKS(GPIO_KEYS_EMIT_POLL_MS));
    }
    internal->emit_task = nullptr;
}

// endregion

// region Driver lifecycle

static void release_keys(GpioKeysInternal* internal) {
    for (uint8_t i = 0; i < internal->key_count; i++) {
        gpio_descriptor_release(internal->keys[i].descriptor);
    }
    internal->key_count = 0;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    if (config->pins_count != config->key_codes_length) {
        LOG_E(TAG, "pins (%u) and key-codes (%u) must have the same length", (unsigned)config->pins_count, (unsigned)config->key_codes_length);
        return ERROR_INVALID_ARGUMENT;
    }
    if (config->pins_count == 0 || config->pins_count > GPIO_KEYS_MAX) {
        LOG_E(TAG, "Unsupported key count %u (max %d)", (unsigned)config->pins_count, GPIO_KEYS_MAX);
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new(std::nothrow) GpioKeysInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    uint32_t now = get_millis();
    for (uint8_t i = 0; i < config->pins_count; i++) {
        const auto& pin = config->pins[i];
        auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, pin.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
        if (descriptor == nullptr) {
            LOG_E(TAG, "Failed to acquire GPIO %u", (unsigned)pin.pin);
            release_keys(internal);
            delete internal;
            return ERROR_RESOURCE;
        }

        // get_level() already applies GPIO_FLAG_ACTIVE_LOW, so "high" means "pressed".
        bool pressed = false;
        gpio_descriptor_get_level(descriptor, &pressed);
        // Seeded with the current level so start-up generates no event. A key held down during
        // boot (e.g. while flashing) only reports its release.
        internal->keys[i] = {
            .descriptor = descriptor,
            .debounced_pressed = pressed,
            .last_raw_pressed = pressed,
            .last_raw_change_time = now,
        };
        internal->key_count++;
        LOG_I(TAG, "%s key %u: GPIO %u, code 0x%04X, level at start: %s", device->name, (unsigned)i, (unsigned)pin.pin, (unsigned)config->key_codes[i], pressed ? "pressed" : "released");
    }

    device_set_driver_data(device, internal);

    if (config->emit_only) {
        error_t error = start_emit_task(device, internal);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to start polling task");
            device_set_driver_data(device, nullptr);
            release_keys(internal);
            delete internal;
            return error;
        }
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<GpioKeysInternal*>(device_get_driver_data(device));
    stop_emit_task(internal);
    release_keys(internal);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static error_t gpio_keys_read_key(Device* device, KeyboardKeyData* data) {
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<GpioKeysInternal*>(device_get_driver_data(device));

    data->key = 0;
    data->pressed = false;
    data->continue_reading = false;

    // emit-only keys are delivered to subscribers by the polling task, never to readers
    if (config->emit_only) {
        return ERROR_NONE;
    }

    poll_keys(config, internal);

    GpioKeysPendingEvent event;
    if (pop_pending(internal, &event)) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = internal->pending_count > 0;
    }

    return ERROR_NONE;
}

// Only consulted to decide whether to show the on-screen keyboard: navigation-only keys
// (text-input false) must not hide it, or text fields can't be filled in at all.
static bool gpio_keys_is_present(Device* device) {
    return GET_CONFIG(device)->text_input;
}

// endregion

static constexpr KeyboardApi gpio_keys_api = {
    .read_key = gpio_keys_read_key,
    .get_backlight = nullptr,
    .is_present = gpio_keys_is_present,
};

Driver gpio_keys_driver = {
    .name = "gpio_keys",
    .compatible = (const char*[]) { "tactility,gpio-keys", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &gpio_keys_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &gpio_keys_module,
    .internal = nullptr
};
