// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Challenges.h"

#include <lvgl.h>

// Shared builder for the challenge-list screen (used by both the Offensive and Defensive apps):
// a header + category breadcrumb, a left list of challenges (name + difficulty) and a right detail
// panel (name / difficulty / story / objectives / points / status), plus a footer. Parameterised by
// the challenge array, its accent colour and the category label so the two categories stay identical.
namespace ot::ui {

void buildChallengeList(lv_obj_t* parent, const ChallengeDef* arr, int count,
                        lv_color_t accent, const char* category);

} // namespace ot::ui
