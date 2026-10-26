// SPDX-License-Identifier: Apache-2.0
#include "Goose.h"

namespace ot::net::goose {

namespace {
void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
uint16_t get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t get32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
} // namespace

size_t encode(const ot::goose::Pdu& p, uint8_t* buf, size_t cap) {
    if (cap < FRAME_LEN) return 0;
    buf[0] = 'G'; buf[1] = 'S'; buf[2] = 0x01;
    buf[3] = (uint8_t)((p.breakerOpen ? 0x01 : 0x00) | (p.test ? 0x02 : 0x00));
    put16(buf + 4, p.appid);
    put32(buf + 6, p.stNum);
    put32(buf + 10, p.sqNum);
    put16(buf + 14, p.confRev);
    return FRAME_LEN;
}

bool parse(const uint8_t* buf, size_t len, ot::goose::Pdu& out) {
    if (buf == nullptr || len != FRAME_LEN) return false;
    if (buf[0] != 'G' || buf[1] != 'S' || buf[2] != 0x01) return false;
    out.breakerOpen = (buf[3] & 0x01) != 0;
    out.test        = (buf[3] & 0x02) != 0;
    out.appid   = get16(buf + 4);
    out.stNum   = get32(buf + 6);
    out.sqNum   = get32(buf + 10);
    out.confRev = get16(buf + 14);
    return true;
}

} // namespace ot::net::goose
