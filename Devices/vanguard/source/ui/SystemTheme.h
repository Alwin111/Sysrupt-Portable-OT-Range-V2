// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

namespace vanguard::theme {

/**
 * Applies the Vanguard look to the whole UI of @a display: every Tactility app, control, the
 * status bar and the on-screen keyboard. Styles of widgets that already exist are added to (never
 * removed), and widgets created later are styled through an LVGL theme.
 * Does nothing when it's already installed on @a display. Caller holds the LVGL lock.
 */
void installSystemTheme(lv_display_t* display);

}
