// SPDX-License-Identifier: Apache-2.0
// Host tests for the pure Modbus/TCP codec.
// g++ -std=c++17 -I../source/ot ot_modbus_tests.cpp ../source/ot/net/Modbus.cpp && ./a.out
#include "net/Modbus.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace ot::modbus;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* file, int line, const char* expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    g_fail++;
}

// A tiny register file: holding 0..9 writable, input mirrors address*10.
struct TestHandler : Handler {
    uint16_t holding[16] = {};
    uint16_t readHolding(uint16_t a) override { return a < 16 ? holding[a] : 0; }
    uint16_t readInput(uint16_t a) override { return (uint16_t)(a * 10); }
    bool writeHolding(uint16_t a, uint16_t v) override {
        if (a >= 10) return false; // 10..15 read-only
        holding[a] = v;
        return true;
    }
};

std::vector<uint8_t> adu(uint8_t fc, std::vector<uint8_t> pduData) {
    std::vector<uint8_t> v = { 0x12, 0x34, 0, 0, 0, 0, 0x01, fc };
    for (auto b : pduData) v.push_back(b);
    const uint16_t len = (uint16_t)(v.size() - 6);
    v[4] = (uint8_t)(len >> 8);
    v[5] = (uint8_t)(len & 0xFF);
    return v;
}
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

int main() {
    TestHandler h;
    uint8_t resp[MAX_ADU];

    // Transaction id and unit id are echoed; protocol id is zero.
    auto req = adu(0x03, { 0x00, 0x00, 0x00, 0x02 }); // read holding 0..1
    size_t n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(n == MBAP_LEN + 2 + 4);
    CHECK(resp[0] == 0x12 && resp[1] == 0x34);
    CHECK(resp[2] == 0 && resp[3] == 0);
    CHECK(resp[6] == 0x01);
    CHECK(resp[7] == 0x03 && resp[8] == 0x04); // fc, byte count

    // Write single, then read it back.
    req = adu(0x06, { 0x00, 0x05, 0xBE, 0xEF });
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(n == MBAP_LEN + 5);
    CHECK(h.holding[5] == 0xBEEF);
    req = adu(0x03, { 0x00, 0x05, 0x00, 0x01 });
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(resp[9] == 0xBE && resp[10] == 0xEF);

    // Input registers (FC04) are the mirror, read-only.
    req = adu(0x04, { 0x00, 0x03, 0x00, 0x01 });
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(resp[7] == 0x04);
    CHECK(((resp[9] << 8) | resp[10]) == 30);

    // Write multiple.
    req = adu(0x10, { 0x00, 0x00, 0x00, 0x02, 0x04, 0x11, 0x11, 0x22, 0x22 });
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(n == MBAP_LEN + 5);
    CHECK(h.holding[0] == 0x1111 && h.holding[1] == 0x2222);

    // Write to a read-only address -> illegal data address exception.
    req = adu(0x06, { 0x00, 0x0A, 0x00, 0x01 });
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(resp[7] == (0x06 | 0x80));
    CHECK(resp[8] == 0x02);

    // Unknown function -> illegal function.
    req = adu(0x2B, { 0x00 });
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(resp[7] == (0x2B | 0x80));
    CHECK(resp[8] == 0x01);

    // Too-large count -> illegal data value.
    req = adu(0x03, { 0x00, 0x00, 0x00, 0xFF }); // 255 > 125
    n = process(req.data(), req.size(), resp, sizeof(resp), h);
    CHECK(resp[7] == (0x03 | 0x80));
    CHECK(resp[8] == 0x03);

    // Truncated ADU (length field says more than present) -> drop (0).
    req = adu(0x03, { 0x00, 0x00, 0x00, 0x02 });
    n = process(req.data(), req.size() - 2, resp, sizeof(resp), h);
    CHECK(n == 0);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
