// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/drivers/gpio.h>

// Devicetree properties can't contain expressions, so the most common combination is named here.
#define GPIO_KEYS_FLAG_ACTIVE_LOW_PULL_UP (GPIO_FLAG_ACTIVE_LOW | GPIO_FLAG_PULL_UP)

// Field order must match the binding's property order: the devicetree compiler emits a
// positional initializer.
struct GpioKeysConfig {
    struct GpioPinSpec* pins;
    uint8_t pins_count;
    const uint32_t* key_codes;
    uint32_t key_codes_length;
    uint32_t debounce_ms;
    bool text_input;
    bool emit_only;
};

#ifdef __cplusplus
}
#endif
