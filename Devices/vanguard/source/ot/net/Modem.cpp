// SPDX-License-Identifier: Apache-2.0
#include "Modem.h"

#include <cstdio>
#include <cstring>

namespace ot::net::modem {

namespace {

// Uppercase, strip leading/trailing spaces into `dst` (<= cap-1). Returns length.
size_t normalise(const char* line, size_t len, char* dst, size_t cap) {
    size_t b = 0, e = len;
    while (b < e && (line[b] == ' ' || line[b] == '\t')) b++;
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) e--;
    size_t n = 0;
    for (size_t i = b; i < e && n < cap - 1; i++) {
        char c = line[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[n++] = c;
    }
    dst[n] = '\0';
    return n;
}

bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

size_t put(char* out, size_t cap, const char* s) {
    size_t n = std::strlen(s);
    if (n >= cap) n = cap - 1;
    std::memcpy(out, s, n);
    out[n] = '\0';
    return n;
}

} // namespace

size_t banner(char* out, size_t cap) {
    return put(out, cap,
               "\r\nCONNECT 2400\r\n"
               "SYSRUPT DAM REMOTE ACCESS UNIT v1.2\r\n"
               "legacy dial-in only - SEND 'AT' TO BEGIN\r\n");
}

size_t process(Session& s, dam::DamSystem& d, const char* line, size_t len,
               uint32_t now_ms, char* out, size_t cap) {
    char cmd[48];
    normalise(line, len, cmd, sizeof(cmd));
    if (cmd[0] == '\0') return 0; // blank line: ignore

    switch (s.state) {
        case State::Idle:
            if (eq(cmd, "AT")) { s.state = State::Ready; return put(out, cap, "OK\r\n"); }
            s.state = State::Idle;
            return put(out, cap, "NO CARRIER\r\n");

        case State::Ready:
            if (eq(cmd, "DIAL") || eq(cmd, "ATD")) {
                s.state = State::Menu;
                s.invalid = 0;
                return put(out, cap,
                           "CONNECTED\r\nGATE CONTROL MENU\r\n"
                           "CMDS: STATUS | READ GATE | SET GATE <0-100>\r\n");
            }
            s.state = State::Idle;
            return put(out, cap, "NO CARRIER\r\n");

        case State::Menu: {
            if (eq(cmd, "STATUS")) {
                char buf[96];
                int n = snprintf(buf, sizeof(buf), "GATE=%u TARGET=%u HELD=%s\r\n",
                                 (unsigned)d.gate(), (unsigned)dam::TARGET_GATE,
                                 d.solved() ? "YES" : (d.atTarget() ? "holding" : "no"));
                if (d.solved()) {
                    snprintf(buf + n, sizeof(buf) - (size_t)n, "FLAG: %s\r\n", d.flag());
                }
                return put(out, cap, buf);
            }
            if (eq(cmd, "READ GATE")) {
                char buf[32];
                snprintf(buf, sizeof(buf), "GATE=%u\r\n", (unsigned)d.gate());
                return put(out, cap, buf);
            }
            if (std::strncmp(cmd, "SET GATE", 8) == 0) {
                const char* p = cmd + 8;
                while (*p == ' ') p++;
                if (*p < '0' || *p > '9') return put(out, cap, "ERR: SET GATE <0-100>\r\n");
                int v = 0;
                for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
                if (v > 100) return put(out, cap, "ERR: range 0-100\r\n");
                d.setGate((uint8_t)v, now_ms);
                char buf[96];
                int n = snprintf(buf, sizeof(buf), "GATE SET %d\r\n", v);
                if (d.solved()) snprintf(buf + n, sizeof(buf) - (size_t)n, "FLAG: %s\r\n", d.flag());
                return put(out, cap, buf);
            }
            // unknown command: rate-limit; three strikes drops the line.
            if (++s.invalid >= 3) {
                s.state = State::Idle;
                s.invalid = 0;
                return put(out, cap, "LINE DROPPED\r\n");
            }
            return put(out, cap, "?\r\n");
        }
    }
    return 0;
}

} // namespace ot::net::modem
