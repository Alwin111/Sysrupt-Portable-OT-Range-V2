// SPDX-License-Identifier: Apache-2.0
#pragma once
// Pure-LVGL builder for the Challenge Complete screen. No app/service dependencies, so the host
// render harness can invoke the exact production widget code with deterministic values.
#include <lvgl.h>

namespace ot::ui {

struct CompleteButtons {
    lv_obj_t* next = nullptr;
    lv_obj_t* list = nullptr;
};

// Builds the full screen (header, metadata, panel, trophy, heading, description, separator,
// two score panels, two action buttons) onto `parent`, using measured 320x240 geometry.
// Returns the two button objects so the caller can attach input callbacks and focus.
CompleteButtons buildCompleteScreen(lv_obj_t* parent,
                                    bool offensive,
                                    const char* categoryTitle, // breadcrumb challenge name
                                    const char* difficulty,    // e.g. "EASY" / "MED" / "HARD"
                                    int pointsAwarded,
                                    int netScore,
                                    int scoreMax,
                                    bool hasNext);

} // namespace ot::ui
