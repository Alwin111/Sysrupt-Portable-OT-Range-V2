// SPDX-License-Identifier: Apache-2.0
#include "HistProto.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ot::net::histproto {

namespace {

// Append a printf-formatted chunk to out[*n], never overflowing outCap (leaves room for NUL).
void append(char* out, size_t& n, size_t outCap, const char* fmt, ...) {
    if (n >= outCap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + n, outCap - n, fmt, ap);
    va_end(ap);
    if (w > 0) {
        n += (size_t)w;
        if (n >= outCap) n = outCap - 1;
    }
}

bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

// Split "CMD arg" -> verb + arg (arg may be empty). Writes lowercased verb into `verb`.
void parse(const char* line, size_t len, char* verb, size_t verbCap, char* arg, size_t argCap) {
    size_t i = 0;
    while (i < len && std::isspace((unsigned char)line[i])) i++;
    size_t v = 0;
    while (i < len && !std::isspace((unsigned char)line[i]) && v + 1 < verbCap)
        verb[v++] = (char)std::tolower((unsigned char)line[i++]);
    verb[v] = '\0';
    while (i < len && std::isspace((unsigned char)line[i])) i++;
    size_t a = 0;
    while (i < len && line[i] != '\r' && line[i] != '\n' && a + 1 < argCap) arg[a++] = line[i++];
    while (a > 0 && std::isspace((unsigned char)arg[a - 1])) a--; // trim trailing
    arg[a] = '\0';
}

int findTag(hist::Historian& h, const char* name) {
    for (int t = 0; t < h.tagCount(); t++) {
        if (ieq(h.tagName(t), name)) return t;
    }
    return -1;
}

} // namespace

size_t processLine(hist::Historian& h, const char* line, size_t len, char* out, size_t outCap) {
    char verb[16], arg[40];
    parse(line, len, verb, sizeof(verb), arg, sizeof(arg));
    size_t n = 0;

    if (verb[0] == '\0') {
        return 0;
    }
    if (ieq(verb, "help")) {
        append(out, n, outCap, "cmds: HELP TAGS READ <tag> RAW <tag> AUDIT\n");
    } else if (ieq(verb, "tags")) {
        append(out, n, outCap, "tags:\n");
        for (int t = 0; t < h.tagCount(); t++) append(out, n, outCap, "  %s\n", h.tagName(t));
    } else if (ieq(verb, "audit")) {
        append(out, n, outCap, "audit trail:\n");
        for (int i = 0; i < h.auditCount(); i++) append(out, n, outCap, "  %s\n", h.auditLine(i));
    } else if (ieq(verb, "read") || ieq(verb, "raw")) {
        const bool raw = ieq(verb, "raw");
        int t = findTag(h, arg);
        if (t < 0) {
            append(out, n, outCap, "ERR no such tag '%s' (try TAGS)\n", arg);
        } else {
            append(out, n, outCap, "%s %s:\n", raw ? "RAW" : "READ", h.tagName(t));
            for (int i = 0; i < h.rowCount(t); i++) {
                const hist::Row& r = h.row(t, i);
                if (r.deleted && !raw) continue; // clean view hides purged rows
                if (r.deleted) {
                    append(out, n, outCap, "  seq=%u val=%d [DELETED]%s%s\n",
                           (unsigned)r.seq, (int)r.value,
                           r.note ? " note=" : "", r.note ? r.note : "");
                } else {
                    append(out, n, outCap, "  seq=%u val=%d\n", (unsigned)r.seq, (int)r.value);
                }
            }
            // Recovering the purged record on the tampered tag solves the challenge.
            if (raw && h.isTamperTag(t)) h.reveal();
        }
    } else {
        append(out, n, outCap, "ERR unknown '%s' (try HELP)\n", verb);
    }
    return n;
}

} // namespace ot::net::histproto
