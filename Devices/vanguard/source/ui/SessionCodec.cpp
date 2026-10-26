// SPDX-License-Identifier: Apache-2.0
#include "SessionCodec.h"

#include <cstdio>
#include <cstdlib>

namespace ot::ui {

int sessionSerialize(const SessionState& s, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return -1;
    int count = s.revealCount;
    if (count < 0) count = 0;
    if (count > SESSION_MAX_REVEALS) count = SESSION_MAX_REVEALS;

    int n = std::snprintf(out, outSize, "%u %u %d", (unsigned)SESSION_VERSION,
                          (unsigned)s.completedMask, count);
    if (n < 0 || (size_t)n >= outSize) return -1;
    size_t pos = (size_t)n;
    for (int i = 0; i < count; i++) {
        int w = std::snprintf(out + pos, outSize - pos, " %d %d", s.reveals[i].num, s.reveals[i].idx);
        if (w < 0 || pos + (size_t)w >= outSize) return -1;
        pos += (size_t)w;
    }
    return (int)pos;
}

bool sessionDeserialize(const char* in, SessionState& out) {
    out = SessionState {}; // reset to defaults; stays this way on any failure
    if (in == nullptr) return false;

    char* end = nullptr;
    unsigned long ver = std::strtoul(in, &end, 10);
    if (end == in || ver != SESSION_VERSION) return false;

    char* p = end;
    unsigned long mask = std::strtoul(p, &end, 10);
    if (end == p) return false;
    p = end;

    long count = std::strtol(p, &end, 10);
    if (end == p || count < 0 || count > SESSION_MAX_REVEALS) return false;
    p = end;

    SessionState parsed;
    parsed.version = SESSION_VERSION;
    parsed.completedMask = (uint32_t)mask;
    parsed.revealCount = (int)count;
    for (int i = 0; i < (int)count; i++) {
        long num = std::strtol(p, &end, 10);
        if (end == p) return false;
        p = end;
        long idx = std::strtol(p, &end, 10);
        if (end == p) return false;
        p = end;
        // Range-check so corrupt data can never index out of bounds downstream.
        if (num < 0 || num > 31 || idx < 0 || idx > 1000) return false;
        parsed.reveals[i].num = (int)num;
        parsed.reveals[i].idx = (int)idx;
    }
    out = parsed;
    return true;
}

} // namespace ot::ui
