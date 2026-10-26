// SPDX-License-Identifier: Apache-2.0
// Host tests for the Historian Forensics model + query protocol (Challenge 6).
// Run: g++ -std=c++17 -I../source/ot ot_historian_tests.cpp ../source/ot/sim/Historian.cpp \
//        ../source/ot/net/HistProto.cpp && ./a.out
#include "net/HistProto.h"
#include "sim/Historian.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace ot;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static std::string run(hist::Historian& h, const char* line) {
    char out[512];
    size_t n = net::histproto::processLine(h, line, std::strlen(line), out, sizeof(out));
    return std::string(out, n);
}

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static void testTagsAndCleanReadHidesPurged() {
    hist::Historian h; h.reset();
    std::string tags = run(h, "TAGS");
    CHECK(has(tags, "REACTOR.TEMP"));
    CHECK(has(tags, "REACTOR.SP"));
    CHECK(has(tags, "PUMP.FLOW"));
    // Clean READ of the tampered tag must NOT show seq 41 (the purged row) or the flag.
    std::string rd = run(h, "READ REACTOR.SP");
    CHECK(has(rd, "seq=40"));
    CHECK(!has(rd, "seq=41"));
    CHECK(!has(rd, "flag{"));
    CHECK(!h.solved());
}

static void testAuditPointsAtPurge() {
    hist::Historian h; h.reset();
    std::string a = run(h, "AUDIT");
    CHECK(has(a, "PURGED"));
    CHECK(has(a, "REACTOR.SP"));
}

static void testRawRecoversFlagAndSolves() {
    hist::Historian h; h.reset();
    std::string raw = run(h, "RAW REACTOR.SP");
    CHECK(has(raw, "seq=41"));
    CHECK(has(raw, "[DELETED]"));
    CHECK(has(raw, hist::HIST_FLAG));
    CHECK(h.solved()); // recovering the purged record solves the challenge
}

static void testRawOnCleanTagDoesNotSolve() {
    hist::Historian h; h.reset();
    std::string raw = run(h, "RAW PUMP.FLOW");
    CHECK(!has(raw, "flag{"));
    CHECK(!h.solved());
}

static void testCaseInsensitiveAndUnknownTag() {
    hist::Historian h; h.reset();
    CHECK(has(run(h, "raw reactor.sp"), hist::HIST_FLAG)); // case-insensitive
    CHECK(has(run(h, "READ NOPE"), "no such tag"));
}

int main() {
    testTagsAndCleanReadHidesPurged();
    testAuditPointsAtPurge();
    testRawRecoversFlagAndSolves();
    testRawOnCleanTagDoesNotSolve();
    testCaseInsensitiveAndUnknownTag();
    printf("historian tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
