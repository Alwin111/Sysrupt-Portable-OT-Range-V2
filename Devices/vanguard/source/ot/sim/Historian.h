// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Challenge 6 - Historian Forensics. Pure logic (host-tested); the text query protocol (net/HistProto)
// is a thin view onto this state.
//
// Scenario: a process historian logs tag samples plus an audit trail. An intruder drove an unsafe
// setpoint, then PURGED the offending samples so the normal (clean) view hides them. The forensic
// task is to notice the tampering (a gap in the sequence numbers / an audit entry marked PURGED) and
// pull the RAW view of the affected tag, which still carries the soft-deleted rows -- one of which
// holds the flag in its operator note. RAW of any other tag reveals nothing.
//
// Solve = the player requests RAW of the tampered tag (recovers the deleted record); the flag is then
// visible in that row and the challenge latches solved.
namespace ot::hist {

constexpr uint16_t PORT = 9000;

constexpr const char* HIST_FLAG = "flag{h1st0r14n_d3l3t3d_r3c0rd}";
constexpr size_t hist_cstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static_assert(hist_cstrlen(HIST_FLAG) <= 32, "HIST_FLAG must fit 32 bytes");

struct Row {
    uint32_t seq;
    int32_t value;
    bool deleted;      // soft-deleted ("purged") - hidden from the clean READ, shown by RAW
    const char* note;  // operator note; the recovered flag lives here on the tampered deleted row
};

class Historian {
public:
    void reset();

    int tagCount() const;
    const char* tagName(int tagId) const;   // nullptr if out of range
    bool isTamperTag(int tagId) const;       // the tag whose RAW view carries the flag

    int rowCount(int tagId) const;
    const Row& row(int tagId, int i) const;  // caller keeps i in [0,rowCount)

    int auditCount() const;
    const char* auditLine(int i) const;      // forensic breadcrumbs (points at the PURGED tag)

    const char* flag() const { return HIST_FLAG; }

    // The net layer calls this when RAW of the tampered tag is served (the deleted record recovered).
    void reveal() { revealed_ = true; }
    bool solved() const { return revealed_; }

private:
    bool revealed_ = false;
};

} // namespace ot::hist
