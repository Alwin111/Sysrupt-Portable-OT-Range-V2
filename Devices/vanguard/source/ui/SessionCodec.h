// SPDX-License-Identifier: Apache-2.0
#pragma once
// Pure (no LVGL / no storage / no services) encode+decode for the persisted session state, so the
// serialization can be unit-tested on the host. The firmware (Challenges.cpp) wraps this with the
// Tactility Preferences file I/O.
#include <cstddef>
#include <cstdint>

namespace ot::ui {

constexpr uint32_t SESSION_VERSION = 1;
constexpr int SESSION_MAX_REVEALS = 32;

struct RevealRec {
    int num = 0; // challenge number the hint belongs to
    int idx = 0; // hint index within that challenge
};

struct SessionState {
    uint32_t version = SESSION_VERSION;
    uint32_t completedMask = 0; // bit n set => challenge number n is completed
    int revealCount = 0;        // number of valid entries in reveals[]
    RevealRec reveals[SESSION_MAX_REVEALS] = {};
};

// Serialize to a compact single line: "<ver> <mask> <count> [<num> <idx>]*".
// Returns the written length (excluding the NUL), or -1 if it would not fit in outSize.
int sessionSerialize(const SessionState& s, char* out, size_t outSize);

// Parse a line produced by sessionSerialize(). Returns true only for a well-formed, in-range,
// current-version payload; on any problem returns false and leaves `out` at its defaults.
bool sessionDeserialize(const char* in, SessionState& out);

} // namespace ot::ui
