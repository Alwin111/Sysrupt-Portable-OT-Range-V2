// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

namespace ot::ui {

// The challenge-selection menu (app id "vanguard.menu"). It is the launcher: it lists every
// challenge with its status and lets the player open ANY of them directly (joystick to move,
// OK/SELECT to open, BACK inside a challenge to return). There is no mandatory progression.
extern const AppManifest menu_manifest;

} // namespace ot::ui
