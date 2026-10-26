// SPDX-License-Identifier: Apache-2.0
// Host tests for the Historian SQL console engine (sim/HistSql) - the web-facing SQL view onto the
// same ot::hist::Historian the :9000 protocol uses. Covers the 10 cases in the feature spec plus the
// challenge-integrity invariants (clean view never leaks the flag; recovering the purged row via the
// RAW view latches the SAME solve as the :9000 RAW path).
// Run: g++ -std=c++17 -I../source/ot ot_histsql_tests.cpp ../source/ot/sim/HistSql.cpp \
//        ../source/ot/sim/Historian.cpp && ./a.out
#include "sim/HistSql.h"
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

static std::string run(hist::Historian& h, const char* sql) {
    char out[4096];
    size_t n = sim::histsql::execute(h, sql, std::strlen(sql), out, sizeof(out));
    return std::string(out, n);
}

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Extract the integer after the first occurrence of "rowCount": (-1 if absent).
static long rowCount(const std::string& s) {
    auto k = s.find("\"rowCount\":");
    if (k == std::string::npos) return -1;
    return std::strtol(s.c_str() + k + 11, nullptr, 10);
}

// 1. Valid SELECT * (clean view): returns columns + rows, never the purged row or the flag.
static void testSelectStar() {
    hist::Historian h; h.reset();
    std::string r = run(h, "SELECT * FROM samples");
    CHECK(has(r, "\"columns\":[\"tag\",\"seq\",\"value\",\"note\"]"));
    CHECK(has(r, "REACTOR.SP"));
    CHECK(has(r, "\"execMs\":"));
    CHECK(!has(r, "flag{"));        // clean view never exposes the flag
    CHECK(rowCount(r) == 24);       // 6 + (7-1 purged) + 6 + 6 non-deleted rows
    CHECK(!h.solved());
}

// 2. Nonexistent table -> db-style error.
static void testNoSuchTable() {
    hist::Historian h; h.reset();
    std::string r = run(h, "SELECT * FROM telemetry");
    CHECK(has(r, "\"error\":"));
    CHECK(has(r, "no such table: telemetry"));
    CHECK(rowCount(r) == 0);
}

// 3. Nonexistent column -> db-style error.
static void testNoSuchColumn() {
    hist::Historian h; h.reset();
    CHECK(has(run(h, "SELECT bogus FROM samples"), "no such column: bogus"));
    CHECK(has(run(h, "SELECT * FROM samples WHERE bogus = '1'"), "no such column: bogus"));
}

// 4. Query returning zero rows -> clean empty result, not an error.
static void testZeroRows() {
    hist::Historian h; h.reset();
    std::string r = run(h, "SELECT * FROM samples WHERE tag = 'DOES.NOT.EXIST'");
    CHECK(!has(r, "\"error\":"));
    CHECK(has(r, "\"rows\":[]"));
    CHECK(rowCount(r) == 0);
}

// 5. WHERE predicate filters rows (case-insensitive on keyword, table and value).
static void testWhere() {
    hist::Historian h; h.reset();
    std::string r = run(h, "select tag,seq,value from samples where tag = 'reactor.sp'");
    CHECK(has(r, "\"columns\":[\"tag\",\"seq\",\"value\"]"));
    CHECK(has(r, "REACTOR.SP"));
    CHECK(!has(r, "PUMP.FLOW"));
    CHECK(!has(r, "\"41\""));       // purged row still hidden by the clean view
    CHECK(rowCount(r) == 6);        // seq 37,38,39,40,42,43
    CHECK(!has(r, "flag{"));
    CHECK(!h.solved());
}

// 6. LIMIT bounds the result.
static void testLimit() {
    hist::Historian h; h.reset();
    CHECK(rowCount(run(h, "SELECT * FROM raw_samples LIMIT 3")) == 3);
    CHECK(rowCount(run(h, "SELECT * FROM raw_samples LIMIT 0")) == 0);
}

// 7. Malformed SQL -> syntax error.
static void testMalformed() {
    hist::Historian h; h.reset();
    CHECK(has(run(h, "SELCT * FROM samples"), "syntax error"));
    CHECK(has(run(h, "SELECT FROM samples"), "syntax error"));
    CHECK(has(run(h, "SELECT *"), "syntax error"));          // missing FROM
    CHECK(has(run(h, "SELECT * FROM samples WHERE tag 'x'"), "syntax error")); // missing '='
    CHECK(has(run(h, ""), "empty query"));
    CHECK(has(run(h, "DROP TABLE samples"), "syntax error")); // no DDL/DML
}

// 8. Unauthorised data: only the four forensic tables exist; no other-challenge/OS tables, and the
//    clean view can never reach the flag.
static void testUnauthorised() {
    hist::Historian h; h.reset();
    std::string t = run(h, "SHOW TABLES");
    CHECK(has(t, "tags") && has(t, "samples") && has(t, "raw_samples") && has(t, "audit"));
    CHECK(!has(t, "access_log") && !has(t, "turbine") && !has(t, "sqlite_master"));
    CHECK(has(run(h, "SELECT * FROM access_log"), "no such table"));
    CHECK(has(run(h, "SELECT * FROM sqlite_master"), "no such table"));
    // No clean-view query, however phrased, yields the flag or solves.
    CHECK(!has(run(h, "SELECT note FROM samples"), "flag{"));
    CHECK(!has(run(h, "SELECT * FROM samples WHERE note = 'flag'"), "flag{"));
    CHECK(!h.solved());
}

// 9. Large result set: RAW view returns every row across all tags (incl. the purged one).
static void testLargeResult() {
    hist::Historian h; h.reset();
    std::string r = run(h, "SELECT * FROM raw_samples");
    // 6 (TEMP) + 7 (SP incl purged) + 6 (FLOW) + 6 (FREQ) = 25 rows.
    CHECK(rowCount(r) == 25);
    CHECK(has(r, "\"41\""));            // the purged row is present in the raw view
    CHECK(has(r, hist::HIST_FLAG));     // its note carries the flag
}

// 10. Completion logic preserved: recovering the tamper tag's purged row via RAW latches reveal();
//     nothing else does (not SHOW TABLES, not clean reads, not RAW of a clean tag, not a WHERE that
//     filters the purged row out).
static void testSolvePreserved() {
    {   // opening the console / listing tables never solves
        hist::Historian h; h.reset();
        run(h, "SHOW TABLES");
        run(h, "SELECT * FROM tags");
        run(h, "SELECT * FROM audit");
        CHECK(!h.solved());
    }
    {   // clean read of the tamper tag does not solve and shows no flag
        hist::Historian h; h.reset();
        run(h, "SELECT * FROM samples WHERE tag='REACTOR.SP'");
        CHECK(!h.solved());
    }
    {   // RAW of a clean tag does not solve
        hist::Historian h; h.reset();
        std::string r = run(h, "SELECT * FROM raw_samples WHERE tag='PUMP.FLOW'");
        CHECK(!has(r, "flag{"));
        CHECK(!h.solved());
    }
    {   // RAW of the tamper tag recovers the flag AND solves (same latch as :9000 RAW)
        hist::Historian h; h.reset();
        std::string r = run(h, "SELECT * FROM raw_samples WHERE tag='REACTOR.SP'");
        CHECK(has(r, hist::HIST_FLAG));
        CHECK(h.solved());
    }
    {   // a WHERE that filters the purged row OUT must not solve (only a returned row counts)
        hist::Historian h; h.reset();
        std::string r = run(h, "SELECT * FROM raw_samples WHERE seq='40'");
        CHECK(!has(r, "flag{"));
        CHECK(!h.solved());
    }
    {   // case-insensitive full path still solves
        hist::Historian h; h.reset();
        CHECK(has(run(h, "select * from RAW_SAMPLES where TAG = 'reactor.sp'"), hist::HIST_FLAG));
        CHECK(has(run(h, "SELECT 1 FROM tags"), "no such column")); // '1' is not a column of tags
    }
}

// Audit table carries the forensic breadcrumb that points at the purge.
static void testAudit() {
    hist::Historian h; h.reset();
    std::string r = run(h, "SELECT * FROM audit");
    CHECK(has(r, "PURGED"));
    CHECK(has(r, "REACTOR.SP"));
}

// Trailing semicolon is accepted.
static void testSemicolon() {
    hist::Historian h; h.reset();
    CHECK(!has(run(h, "SELECT * FROM tags;"), "\"error\""));
    CHECK(has(run(h, "SHOW TABLES;"), "raw_samples"));
}

// ---- extended grammar (operators, AND/OR, ORDER BY, DISTINCT, COUNT, LIKE, DESCRIBE) ----

static void testDescribe() {
    hist::Historian h; h.reset();
    std::string r = run(h, "DESCRIBE samples");
    CHECK(has(r, "\"columns\":[\"column\",\"type\"]"));
    CHECK(has(r, "seq") && has(r, "integer") && has(r, "note") && has(r, "text"));
    CHECK(has(run(h, "DESCRIBE nope"), "no such table"));
}

static void testComparisonOperators() {
    hist::Historian h; h.reset();
    // > on a numeric column, isolated to the tamper tag -> recovers the purged row (value 980)
    std::string r = run(h, "SELECT * FROM raw_samples WHERE tag='REACTOR.SP' AND value > 900");
    CHECK(rowCount(r) == 1 && has(r, hist::HIST_FLAG) && h.solved());
    // >= boundary
    hist::Historian h2; h2.reset();
    CHECK(has(run(h2, "SELECT * FROM raw_samples WHERE tag='REACTOR.SP' AND value >= 980"), hist::HIST_FLAG));
    // != excludes the purged row -> no flag, not solved
    hist::Historian h3; h3.reset();
    std::string r3 = run(h3, "SELECT * FROM raw_samples WHERE tag='REACTOR.SP' AND seq != 41");
    CHECK(rowCount(r3) == 6 && !has(r3, "flag{") && !h3.solved());
    // deleted >= 1 retrieves the purged row
    hist::Historian h4; h4.reset();
    CHECK(has(run(h4, "SELECT * FROM raw_samples WHERE tag='REACTOR.SP' AND deleted >= 1"), hist::HIST_FLAG));
}

static void testAndOr() {
    hist::Historian h; h.reset();
    CHECK(rowCount(run(h, "SELECT * FROM samples WHERE tag='REACTOR.SP' AND seq >= 42")) == 2);
    CHECK(rowCount(run(h, "SELECT * FROM samples WHERE tag='PUMP.FLOW' OR tag='GRID.FREQ'")) == 12);
    CHECK(has(run(h, "SELECT * FROM samples WHERE tag='X' AND seq=1 OR tag='Y'"), "mixed AND/OR"));
}

static void testOrderByDistinctRejected() {
    // ORDER BY and DISTINCT require buffering the whole result set, which the streaming engine does
    // not do on this memory-constrained device: they are rejected cleanly (no crash, no solve).
    hist::Historian h; h.reset();
    std::string ob = run(h, "SELECT seq,value FROM raw_samples WHERE tag='REACTOR.SP' ORDER BY value DESC LIMIT 1");
    CHECK(has(ob, "\"error\"") && has(ob, "ORDER BY") && !h.solved());
    hist::Historian h2; h2.reset();
    std::string ds = run(h2, "SELECT DISTINCT value FROM samples WHERE tag='REACTOR.SP'");
    CHECK(has(ds, "\"error\"") && has(ds, "DISTINCT") && !h2.solved());
    // LIMIT (without ordering) still streams fine and bounds the output.
    hist::Historian h3; h3.reset();
    CHECK(rowCount(run(h3, "SELECT * FROM raw_samples LIMIT 3")) == 3);
}

static void testCountStar() {
    hist::Historian h; h.reset();
    CHECK(has(run(h, "SELECT COUNT(*) FROM raw_samples"), "\"25\""));
    CHECK(has(run(h, "SELECT COUNT(*) FROM raw_samples WHERE tag='REACTOR.SP'"), "\"7\""));
    // COUNT counts the purged row but never returns it -> must NOT solve and must not leak the flag
    hist::Historian h2; h2.reset();
    std::string r = run(h2, "SELECT COUNT(*) FROM raw_samples WHERE deleted = 1");
    CHECK(has(r, "\"1\"") && !has(r, "flag{") && !h2.solved());
}

static void testLike() {
    hist::Historian h; h.reset();
    CHECK(rowCount(run(h, "SELECT * FROM tags WHERE name LIKE 'REACTOR%'")) == 2);
    CHECK(rowCount(run(h, "SELECT * FROM tags WHERE name LIKE '%.SP'")) == 1);
    CHECK(rowCount(run(h, "SELECT * FROM tags WHERE name LIKE '%FLOW%'")) == 1);
}

// The vulnerable tag-search: normal input is scoped to committed rows; an injection that neutralises
// the "AND deleted = 0" scope guard escapes it and recovers the purged tamper row (latches reveal()).
static void testSqlInjectionSearch() {
    char out[2048];
    auto search = [&](hist::Historian& h, const char* in) {
        return sim::histsql::searchTag(h, in, std::strlen(in), out, sizeof(out));
    };
    // Normal tag search: committed samples only; the purged incident row stays hidden; no solve.
    { hist::Historian h; h.reset(); search(h, "REACTOR.SP");
      CHECK(!h.solved()); CHECK(!std::strstr(out, hist::HIST_FLAG)); CHECK(std::strstr(out, "480")); }
    // Tautology + comment injection escapes the scope, returns the purged row, latches the solve.
    { hist::Historian h; h.reset(); search(h, "REACTOR.SP' OR '1'='1' -- ");
      CHECK(h.solved()); CHECK(std::strstr(out, hist::HIST_FLAG)); }
    // A non-tautological injection leaks nothing and does not solve.
    { hist::Historian h; h.reset(); search(h, "x' OR '1'='2");
      CHECK(!h.solved()); CHECK(!std::strstr(out, hist::HIST_FLAG)); }
    // A seq-based tautology works too (classic numeric injection).
    { hist::Historian h; h.reset(); search(h, "x' OR seq > 0 -- ");
      CHECK(h.solved()); CHECK(std::strstr(out, hist::HIST_FLAG)); }
    // Searching a clean tag never solves (no purged rows to escape to).
    { hist::Historian h; h.reset(); search(h, "PUMP.FLOW");
      CHECK(!h.solved()); CHECK(!std::strstr(out, hist::HIST_FLAG)); }
}

int main() {
    testDescribe();
    testSqlInjectionSearch();
    testComparisonOperators();
    testAndOr();
    testOrderByDistinctRejected();
    testCountStar();
    testLike();
    testSelectStar();
    testNoSuchTable();
    testNoSuchColumn();
    testZeroRows();
    testWhere();
    testLimit();
    testMalformed();
    testUnauthorised();
    testLargeResult();
    testSolvePreserved();
    testAudit();
    testSemicolon();
    printf("histsql tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
