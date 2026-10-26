// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For lv_draw_buf_t's data field
#include "ScreenCapture.h"

#include <tactility/log.h>

#include <cstdio>

// Developer screen capture, streamed over the USB console so the UI can be reviewed from a host
// (Vanguard_OT_Range/tools/vgshot.py turns it into a PNG). It needs no extra memory: the whole
// screen is invalidated once, and every strip LVGL flushes during that refresh is printed while
// it's still in the draw buffer, as base64 RGB565 (little endian):
//   VGSHOT BEGIN <width> <height>
//   VGSHOT A <x1> <y1> <x2> <y2> <base64 pixels>
//   VGSHOT END
namespace vanguard::screencapture {

namespace {

constexpr auto* TAG = "ScreenCapture";

bool capturing = false;

void printBase64(const uint8_t* data, size_t size) {
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[4];
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t remaining = size - i;
        const uint32_t chunk = (data[i] << 16) |
            (remaining > 1 ? data[i + 1] << 8 : 0) |
            (remaining > 2 ? data[i + 2] : 0);
        out[0] = ALPHABET[(chunk >> 18) & 0x3F];
        out[1] = ALPHABET[(chunk >> 12) & 0x3F];
        out[2] = remaining > 1 ? ALPHABET[(chunk >> 6) & 0x3F] : '=';
        out[3] = remaining > 2 ? ALPHABET[chunk & 0x3F] : '=';
        fwrite(out, 1, 4, stdout);
    }
}

void onFlushStart(lv_event_t* e) {
    auto* display = static_cast<lv_display_t*>(lv_event_get_target(e));
    const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(e));
    const lv_draw_buf_t* buffer = lv_display_get_buf_active(display);
    if (area == nullptr || buffer == nullptr) {
        return;
    }
    const size_t size = static_cast<size_t>(lv_area_get_width(area)) * lv_area_get_height(area) * 2;
    printf("\nVGSHOT A %ld %ld %ld %ld ", (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2);
    printBase64(buffer->data, size);
    printf("\n");
}

void onRenderReady(lv_event_t* e) {
    auto* display = static_cast<lv_display_t*>(lv_event_get_target(e));
    lv_display_remove_event_cb_with_user_data(display, onFlushStart, nullptr);
    lv_display_remove_event_cb_with_user_data(display, onRenderReady, nullptr);
    printf("\nVGSHOT END\n");
    fflush(stdout);
    capturing = false;
    LOG_I(TAG, "Capture done");
}

}

void capture(lv_display_t* display) {
    if (display == nullptr || capturing) {
        return;
    }
    if (lv_display_get_color_format(display) != LV_COLOR_FORMAT_RGB565) {
        LOG_W(TAG, "Only RGB565 displays are supported");
        return;
    }
    capturing = true;
    LOG_I(TAG, "Capturing screen");
    printf("\nVGSHOT BEGIN %ld %ld\n", (long)lv_display_get_horizontal_resolution(display), (long)lv_display_get_vertical_resolution(display));
    lv_display_add_event_cb(display, onFlushStart, LV_EVENT_FLUSH_START, nullptr);
    lv_display_add_event_cb(display, onRenderReady, LV_EVENT_RENDER_READY, nullptr);
    // Redraw everything once, so the strips together cover the whole screen
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_obj_invalidate(lv_display_get_layer_sys(display));
}

}
