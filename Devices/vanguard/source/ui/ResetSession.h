// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

// The Reset Session confirmation screen (app id "vanguard.reset"), reference: UI designs/
// reset_page.png. Opened by the physical DISP button (see module.cpp). It is a destructive confirmation:
// opening it changes nothing; only the explicit RESET SESSION action clears the session (all
// challenge progress, score and hint usage) via ot::ui::resetSession(), run on a worker task. CANCEL
// / BACK preserve everything.
namespace ot::ui {
extern const AppManifest reset_manifest;
}
