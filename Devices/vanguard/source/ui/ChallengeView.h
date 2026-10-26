// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

namespace ot::ui {

// The per-challenge screen (app id "vanguard.challenge"). It renders whichever challenge the menu
// selected (see setSelectedChallenge). BACK returns to the menu. For the reactor challenges it
// ensures the shared range (OtService: AP + HMI + Modbus) is running, and never stops it on close,
// so switching between challenges keeps the network and each challenge's state intact.
extern const AppManifest challenge_manifest;

} // namespace ot::ui
