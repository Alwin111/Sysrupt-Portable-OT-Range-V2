// SPDX-License-Identifier: Apache-2.0
#include "GooseSim.h"

namespace ot::goose {

void GooseSim::reset() {
    seeded_ = false;
    last_st_ = 0;
    breaker_open_ = false;
    solved_ = false;
}

Result GooseSim::process(const Pdu& p) {
    if (p.appid != EXPECTED_APPID) return Result::RejectedAppid;
    if (p.confRev != EXPECTED_CONFREV) return Result::RejectedConfRev;
    if (p.test) return Result::RejectedTest;

    // A frame is a new state change only if stNum is strictly greater than the last accepted. The
    // first ever frame on the subscription seeds the baseline.
    if (seeded_ && p.stNum <= last_st_) return Result::RejectedStale;

    seeded_ = true;
    last_st_ = p.stNum;
    breaker_open_ = p.breakerOpen;
    if (p.breakerOpen) solved_ = true; // an accepted breaker-open state change is the blackout
    return Result::Accepted;
}

} // namespace ot::goose
