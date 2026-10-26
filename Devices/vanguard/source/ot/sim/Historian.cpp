// SPDX-License-Identifier: Apache-2.0
#include "Historian.h"

namespace ot::hist {

namespace {

struct Tag {
    const char* name;
    const Row* rows;
    int rowCount;
    bool tamper;
};

// Tag 1 (REACTOR.SP, the reactor setpoint) is the tampered tag: seq 41 was PURGED after an intruder
// drove the setpoint to an unsafe value. Its RAW view still holds that soft-deleted row, whose note
// carries the flag. The other tags are clean decoys.
constexpr Row ROWS_TEMP[] = {
    { 37, 2951, false, nullptr }, { 38, 2960, false, nullptr },
    { 39, 2958, false, nullptr }, { 40, 2963, false, nullptr },
    { 42, 2970, false, nullptr }, { 43, 2966, false, nullptr },
};
constexpr Row ROWS_SP[] = {
    { 37, 480, false, nullptr },
    { 38, 480, false, nullptr },
    { 39, 480, false, nullptr },
    { 40, 480, false, nullptr },
    // seq 41: the intruder's unsafe setpoint write, soft-deleted to hide it. Recovered by RAW.
    { 41, 980, true,  HIST_FLAG },
    { 42, 480, false, nullptr },
    { 43, 480, false, nullptr },
};
constexpr Row ROWS_FLOW[] = {
    { 37, 1420, false, nullptr }, { 38, 1419, false, nullptr },
    { 39, 1425, false, nullptr }, { 40, 1430, false, nullptr },
    { 41, 1422, false, nullptr }, { 42, 1418, false, nullptr },
};
constexpr Row ROWS_FREQ[] = {
    { 37, 5000, false, nullptr }, { 38, 4999, false, nullptr },
    { 39, 5001, false, nullptr }, { 40, 5000, false, nullptr },
    { 41, 4998, false, nullptr }, { 42, 5002, false, nullptr },
};

constexpr Tag TAGS[] = {
    { "REACTOR.TEMP", ROWS_TEMP, (int)(sizeof(ROWS_TEMP) / sizeof(Row)), false },
    { "REACTOR.SP",   ROWS_SP,   (int)(sizeof(ROWS_SP) / sizeof(Row)),   true },
    { "PUMP.FLOW",    ROWS_FLOW, (int)(sizeof(ROWS_FLOW) / sizeof(Row)), false },
    { "GRID.FREQ",    ROWS_FREQ, (int)(sizeof(ROWS_FREQ) / sizeof(Row)), false },
};
constexpr int TAG_COUNT = (int)(sizeof(TAGS) / sizeof(Tag));

// Forensic breadcrumbs: the audit trail shows a write that is no longer in the clean history, and
// names the tag + seq that was purged. This is the hint that leads to RAW REACTOR.SP.
constexpr const char* AUDIT[] = {
    "seq 39 WRITE REACTOR.SP=480 op=eng01 ok",
    "seq 40 WRITE REACTOR.SP=480 op=eng01 ok",
    "seq 41 WRITE REACTOR.SP=980 op=??? [PURGED]",
    "seq 42 WRITE REACTOR.SP=480 op=eng01 ok",
    "note: purged rows remain in RAW view until vacuum",
};
constexpr int AUDIT_COUNT = (int)(sizeof(AUDIT) / sizeof(AUDIT[0]));

} // namespace

void Historian::reset() { revealed_ = false; }

int Historian::tagCount() const { return TAG_COUNT; }

const char* Historian::tagName(int tagId) const {
    return (tagId >= 0 && tagId < TAG_COUNT) ? TAGS[tagId].name : nullptr;
}

bool Historian::isTamperTag(int tagId) const {
    return (tagId >= 0 && tagId < TAG_COUNT) && TAGS[tagId].tamper;
}

int Historian::rowCount(int tagId) const {
    return (tagId >= 0 && tagId < TAG_COUNT) ? TAGS[tagId].rowCount : 0;
}

const Row& Historian::row(int tagId, int i) const {
    return TAGS[tagId].rows[i];
}

int Historian::auditCount() const { return AUDIT_COUNT; }

const char* Historian::auditLine(int i) const {
    return (i >= 0 && i < AUDIT_COUNT) ? AUDIT[i] : nullptr;
}

} // namespace ot::hist
