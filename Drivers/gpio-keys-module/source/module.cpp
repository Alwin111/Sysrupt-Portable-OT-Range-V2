// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gpio_keys_driver;

static Driver* const gpio_keys_drivers[] = {
    &gpio_keys_driver,
    nullptr
};

Module gpio_keys_module = {
    .name = "gpio-keys",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gpio_keys_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
