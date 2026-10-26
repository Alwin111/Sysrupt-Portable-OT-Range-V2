// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

namespace vanguard::screencapture {

/** Streams the next full refresh of @a display over the USB console. Caller holds the LVGL lock. */
void capture(lv_display_t* display);

}
