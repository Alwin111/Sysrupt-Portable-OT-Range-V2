// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

// The Offensive challenge-list screen (reference: offensive-list.png). A separate app so the badge
// BACK button returns to the category-select launcher; SELECT opens the per-challenge overview.
namespace ot::ui {
extern const AppManifest offensive_manifest;
}
