// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

// The Challenge Complete screen (app id "vanguard.complete"), reference: UI designs/
// chall_completionpage.png. It is launched by ChallengeView when a challenge is genuinely completed
// (the not-completed -> completed edge while the view is open). It renders the just-completed
// challenge from the registry (see setSelectedChallenge) with the real score, and offers NEXT
// CHALLENGE / CHALLENGE LIST. It reads the existing computed score (netScore) and never awards points
// itself, so re-opening it cannot change the score.
namespace ot::ui {
extern const AppManifest complete_manifest;
}
