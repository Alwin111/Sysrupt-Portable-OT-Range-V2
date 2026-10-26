// SPDX-License-Identifier: Apache-2.0
#pragma once
// Pure-LVGL builder for the Reset Session screen. No app/service dependencies, so the host
// render harness can invoke the exact production widget code with deterministic values.
#include <lvgl.h>

namespace ot::ui {

struct ResetButtons {
    lv_obj_t* reset = nullptr;
    lv_obj_t* cancel = nullptr;
};

// Builds the full screen (header, metadata, panel, warning triangle + heading, dashed separator,
// two-line warning text, red summary panel with five triangular-bulleted items, RESET / CANCEL
// buttons with icons, and the footer hint strip). Returns both buttons for callback/focus wiring.
ResetButtons buildResetScreen(lv_obj_t* parent,
                              bool offensive,
                              const char* categoryTitle,
                              const char* difficulty,
                              int pointsValue,
                              int netScore,
                              int scoreMax);

} // namespace ot::ui
