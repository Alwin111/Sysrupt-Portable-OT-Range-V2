// SPDX-License-Identifier: Apache-2.0
#include "Tristation.h"

namespace ot::net::tristation {

size_t requestLen(uint8_t cmd) {
    switch (cmd) {
        case CMD_READ: return 5;     // AA 55 01 reg(2)
        case CMD_WRITE: return 7;    // AA 55 02 reg(2) value(2)
        case CMD_DOWNLOAD: return 8; // AA 55 10 fw(2) key(2) trip(1)
        default: return 0;
    }
}

size_t processFrame(sis::SafetySystem& s, uint32_t now_ms,
                    const uint8_t* in, size_t len, uint8_t* out, size_t outCap) {
    if (len < 3 || in[0] != SYNC0 || in[1] != SYNC1) {
        return 0;
    }
    const uint8_t cmd = in[2];
    if (len != requestLen(cmd)) {
        return 0;
    }

    size_t n = 0;
    auto hdr = [&]() {
        if (outCap < 3) return false;
        out[n++] = SYNC0; out[n++] = SYNC1; out[n++] = cmd;
        return true;
    };

    if (cmd == CMD_READ) {
        const uint16_t reg = (uint16_t)((in[3] << 8) | in[4]);
        const uint16_t val = s.read(reg, now_ms);
        if (outCap < 7 || !hdr()) return 0;
        out[n++] = in[3]; out[n++] = in[4];
        out[n++] = (uint8_t)(val >> 8); out[n++] = (uint8_t)(val & 0xFF);
        return n;
    }
    if (cmd == CMD_WRITE) {
        const uint16_t reg = (uint16_t)((in[3] << 8) | in[4]);
        const uint16_t val = (uint16_t)((in[5] << 8) | in[6]);
        const sis::WriteResult r = s.write(reg, val, now_ms);
        const uint8_t status = (r == sis::WriteResult::Ok) ? 0 : (r == sis::WriteResult::Rejected ? 1 : 2);
        if (outCap < 4 || !hdr()) return 0;
        out[n++] = status;
        return n;
    }
    if (cmd == CMD_DOWNLOAD) {
        const uint16_t fw = (uint16_t)((in[3] << 8) | in[4]);
        const uint16_t key = (uint16_t)((in[5] << 8) | in[6]);
        const bool trip = in[7] != 0;
        const sis::DownloadResult r = s.programDownload(fw, key, trip, now_ms);
        uint8_t status = 0;
        switch (r) {
            case sis::DownloadResult::Accepted: status = 0; break;
            case sis::DownloadResult::Locked: status = 1; break;
            case sis::DownloadResult::BadFirmware: status = 2; break;
            case sis::DownloadResult::BadKey: status = 3; break;
        }
        if (outCap < 4 || !hdr()) return 0;
        out[n++] = status;
        return n;
    }
    return 0;
}

} // namespace ot::net::tristation
