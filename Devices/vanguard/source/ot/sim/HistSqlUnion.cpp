// SPDX-License-Identifier: Apache-2.0
//
// Bounded pattern-matching simulator for the V1 offensive Historian SQL injection. NOT SQLite - see
// HistSqlUnion.h for the full contract, the recognised injection shapes, and what is NOT reproduced.
#include "HistSqlUnion.h"

#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ot::sim::histsqlunion {

namespace {

// ---- static dataset (in flash/.rodata; mirrors V1 services/historian/app) --------------------------

// The vulnerable base query is SELECT * FROM process_data, so result headers are always these 10.
const char* const PD_COLS[10] = {
    "id", "timestamp", "source", "tank_level", "chlorine_ppm",
    "ph", "temperature", "flow_rate", "filter_dp", "distribution_pressure"};

// A small synthetic sample (NOT V1's 1000 rows) across the four sources, enough for a benign filter.
const char* const PD_ROWS[][10] = {
    {"1", "2026-03-06 00:00:00", "intake",       "71.2", "2.50", "7.20", "18.1", "612.4", "5.1", "62.3"},
    {"2", "2026-03-06 00:30:00", "chemical",     "70.8", "2.60", "7.19", "18.0", "598.2", "5.3", "61.9"},
    {"3", "2026-03-06 01:00:00", "filtration",   "69.9", "2.55", "7.21", "17.9", "585.0", "5.6", "61.2"},
    {"4", "2026-03-06 01:30:00", "distribution", "69.1", "2.52", "7.18", "17.8", "560.7", "5.9", "60.4"},
    {"5", "2026-03-06 02:00:00", "intake",       "68.5", "2.49", "7.17", "17.7", "548.3", "6.1", "59.8"},
    {"6", "2026-03-06 02:30:00", "chemical",     "68.0", "2.51", "7.20", "17.6", "537.9", "6.3", "59.1"},
    {"7", "2026-03-06 03:00:00", "filtration",   "67.6", "2.48", "7.22", "17.5", "529.4", "6.6", "58.6"},
    {"8", "2026-03-06 03:30:00", "distribution", "67.2", "2.47", "7.19", "17.4", "521.0", "6.8", "58.0"},
};
constexpr int PD_N = (int)(sizeof(PD_ROWS) / sizeof(PD_ROWS[0]));

const char* const CRED_COLS[7] = {"id", "service", "host", "port", "username", "password", "notes"};
const char* const CRED_ROWS[][7] = {
    {"1", "SCADA HMI",               "10.0.3.10",  "8080", "operator",     "scada_op!",  "Main SCADA operator login"},
    {"2", "Engineering Workstation", "10.0.3.20",  "8080", "openplc",      "openplc",    "OpenPLC web IDE default"},
    {"3", "Jump Host SSH",           "10.0.2.20",  "22",   "maintenance",  "maint2024!", "DMZ jump host for OT access"},
    {"4", "Safety HMI",              "10.0.5.202", "8080", "safety_admin", "s1s_adm1n!", "Safety system admin"},
    {"5", "OPC-UA Gateway",          "10.0.3.20",  "4840", "anonymous",    "",           "Anonymous access enabled"},
    {"6", "Engineering WS SSH",      "10.0.3.20",  "22",   "engineer",     "eng2024!",   "SSH access to engineering workstation"},
};
constexpr int CRED_N = (int)(sizeof(CRED_ROWS) / sizeof(CRED_ROWS[0]));

const char* const USER_COLS[5] = {"id", "username", "password", "name", "role"};
const char* const USER_ROWS[][5] = {
    {"1", "historian", "hist0ry!", "Historian Admin", "admin"},
    {"2", "viewer",    "view2024", "Read-Only Viewer", "viewer"},
};
constexpr int USER_N = (int)(sizeof(USER_ROWS) / sizeof(USER_ROWS[0]));

const char* const FLAG_COLS[3] = {"id", "flag", "description"};
const char* const FLAG_ROWS[][3] = {
    {"1", FLAG, "Found via SQL injection in query interface"},  // FLAG = the single authoritative value
};
constexpr int FLAG_N = (int)(sizeof(FLAG_ROWS) / sizeof(FLAG_ROWS[0]));

// sqlite_master contents: CREATE TABLE text, in creation order (what enumeration/schema return).
struct MasterRow { const char* name; const char* create; };
const MasterRow MASTER[] = {
    {"users",        "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT, name TEXT, role TEXT)"},
    {"process_data", "CREATE TABLE process_data (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT, source TEXT, tank_level REAL, chlorine_ppm REAL, ph REAL, temperature REAL, flow_rate REAL, filter_dp REAL, distribution_pressure REAL)"},
    {"credentials",  "CREATE TABLE credentials (id INTEGER PRIMARY KEY, service TEXT, host TEXT, port INTEGER, username TEXT, password TEXT, notes TEXT)"},
    {"flags",        "CREATE TABLE flags (id INTEGER PRIMARY KEY, flag TEXT, description TEXT)"},
};
constexpr int MASTER_N = (int)(sizeof(MASTER) / sizeof(MASTER[0]));

// ---- bounded output writer (same shape/discipline as sim/HistSql) ----------------------------------

struct Writer { char* out; size_t n; size_t cap; };

void put(Writer& w, const char* fmt, ...) {
    if (w.cap == 0 || w.n >= w.cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(w.out + w.n, w.cap - w.n, fmt, ap);
    va_end(ap);
    if (r < 0) return;
    if ((size_t)r >= w.cap - w.n) w.n = w.cap - 1;
    else w.n += (size_t)r;
}

void putJsonStr(Writer& w, const char* s) {
    put(w, "\"");
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') put(w, "\\%c", c);
        else if (c == '\n') put(w, "\\n");
        else if (c == '\r') put(w, "\\r");
        else if (c == '\t') put(w, "\\t");
        else if (c < 0x20) put(w, "\\u%04x", c);
        else put(w, "%c", c);
    }
    put(w, "\"");
}

using Clock = std::chrono::steady_clock;
uint32_t elapsedMs(Clock::time_point t0) {
    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    return ms < 0 ? 0u : (uint32_t)ms;
}

// ---- small text helpers -----------------------------------------------------------------------------

char lc(char c) { return (char)std::tolower((unsigned char)c); }
bool isIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

void trimRange(const char*& e, size_t& len) {
    while (len && std::isspace((unsigned char)*e)) { e++; len--; }
    while (len && std::isspace((unsigned char)e[len - 1])) len--;
}

bool ciPrefix(const char* e, size_t len, const char* pfx) {
    size_t pl = std::strlen(pfx);
    if (len < pl) return false;
    for (size_t i = 0; i < pl; i++) if (lc(e[i]) != lc(pfx[i])) return false;
    return true;
}

bool ciEqN(const char* a, const char* b, size_t len) {
    for (size_t i = 0; i < len; i++) if (lc(a[i]) != lc(b[i])) return false;
    return true;
}

// Find a standalone, case-insensitive keyword in [s,end); returns a pointer to it or nullptr.
const char* ciWord(const char* s, const char* end, const char* w) {
    size_t wl = std::strlen(w);
    for (const char* p = s; p + wl <= end; p++) {
        if (p > s && isIdent(p[-1])) continue;
        if (p + wl != end && isIdent(p[wl])) continue;
        if (ciEqN(p, w, wl)) return p;
    }
    return nullptr;
}

// ---- column lookup + projection resolution ----------------------------------------------------------

struct Cols { const char* const* names; const char* const* vals; int n; };

const char* lookupCol(const Cols& c, const char* nm, size_t len) {
    for (int i = 0; i < c.n; i++) {
        const char* k = c.names[i];
        if (std::strlen(k) == len && ciEqN(k, nm, len)) return c.vals[i];
    }
    return nullptr;
}

size_t appendRaw(char* d, size_t cap, size_t pos, const char* s, size_t n) {
    for (size_t i = 0; i < n && pos + 1 < cap; i++) d[pos++] = s[i];
    if (cap) d[pos < cap ? pos : cap - 1] = '\0';
    return pos;
}

size_t resolveExpr(char* d, size_t cap, size_t pos, const char* e, size_t len, const Cols& cols);

size_t resolveTerm(char* d, size_t cap, size_t pos, const char* e, size_t len, const Cols& cols) {
    trimRange(e, len);
    if (len >= 2 && (e[0] == '\'' || e[0] == '"')) {      // quoted literal -> inner text
        char q = e[0];
        size_t i = 1;
        while (i < len && e[i] != q) i++;
        return appendRaw(d, cap, pos, e + 1, i - 1);
    }
    const char* v = lookupCol(cols, e, len);              // a column of the target table
    if (v) return appendRaw(d, cap, pos, v, std::strlen(v));
    return appendRaw(d, cap, pos, e, len);                // otherwise echo verbatim (e.g. a bare "1")
}

size_t resolveExpr(char* d, size_t cap, size_t pos, const char* e, size_t len, const Cols& cols) {
    trimRange(e, len);
    if (ciPrefix(e, len, "replace(")) {                   // replace(sql,char(10),' ') -> first arg (sql)
        const char* p = e + 8;
        const char* end = e + len;
        int depth = 1;
        const char* a1 = p;
        while (p < end && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            else if (*p == ',' && depth == 1) break;
            p++;
        }
        return resolveExpr(d, cap, pos, a1, (size_t)(p - a1), cols);
    }
    int depth = 0; char q = 0;                            // top-level "||" concatenation
    for (size_t i = 0; i + 1 < len; i++) {
        char c = e[i];
        if (q) { if (c == q) q = 0; continue; }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '(') depth++;
        else if (c == ')') { if (depth) depth--; }
        else if (depth == 0 && c == '|' && e[i + 1] == '|') {
            pos = resolveExpr(d, cap, pos, e, i, cols);
            return resolveExpr(d, cap, pos, e + i + 2, len - (i + 2), cols);
        }
    }
    return resolveTerm(d, cap, pos, e, len, cols);
}

// ---- parsed SELECT projection list ------------------------------------------------------------------

struct ExprList {
    const char* e[24];
    size_t len[24];
    int n;
};

bool addCol(ExprList& out, const char* s, size_t len) {
    if (out.n >= 24) return false;
    trimRange(s, len);
    out.e[out.n] = s;
    out.len[out.n] = len;
    out.n++;
    return true;
}

// Split a SELECT list on top-level commas (respecting quotes and parens, so replace(sql,char(10),' ')
// counts as ONE column). Returns false if it overflows the column cap.
bool parseCols(const char* s, size_t len, ExprList& out) {
    out.n = 0;
    int depth = 0; char q = 0; size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (q) { if (c == q) q = 0; continue; }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '(') depth++;
        else if (c == ')') { if (depth) depth--; }
        else if (c == ',' && depth == 0) { if (!addCol(out, s + start, i - start)) return false; start = i + 1; }
    }
    return addCol(out, s + start, len - start);
}

void starExprs(ExprList& out) {                           // projection == SELECT * (the 10 PD columns)
    out.n = 10;
    for (int i = 0; i < 10; i++) { out.e[i] = PD_COLS[i]; out.len[i] = std::strlen(PD_COLS[i]); }
}

// ---- row emission -----------------------------------------------------------------------------------

void emitRow(Writer& w, const ExprList& pr, const Cols& ctx, bool& first, int& count) {
    if (!first) put(w, ",");
    first = false;
    put(w, "[");
    for (int i = 0; i < pr.n; i++) {
        if (i) put(w, ",");
        char cell[320];
        cell[0] = '\0';
        resolveExpr(cell, sizeof(cell), 0, pr.e[i], pr.len[i], ctx);
        putJsonStr(w, cell);
    }
    put(w, "]");
    count++;
}

void header(Writer& w) {
    put(w, "{\"columns\":[");
    for (int i = 0; i < 10; i++) { if (i) put(w, ","); putJsonStr(w, PD_COLS[i]); }
    put(w, "],\"rows\":[");
}

size_t finish(Writer& w, int count, Clock::time_point t0) {
    put(w, "],\"rowCount\":%d,\"execMs\":%u}", count, elapsedMs(t0));
    return w.n;
}

size_t emitError(Writer& w, const char* msg, Clock::time_point t0) {
    put(w, "{\"error\":");
    putJsonStr(w, msg);
    put(w, ",\"rowCount\":0,\"execMs\":%u}", elapsedMs(t0));
    return w.n;
}

// Emit process_data rows whose source column equals `val` (benign filter / base-query matches).
void emitProcessDataFiltered(Writer& w, const char* val, size_t vlen, bool& first, int& count) {
    ExprList star; starExprs(star);
    for (int i = 0; i < PD_N; i++) {
        const char* src = PD_ROWS[i][2];
        if (std::strlen(src) == vlen && ciEqN(src, val, vlen)) {
            Cols ctx{PD_COLS, PD_ROWS[i], 10};
            emitRow(w, star, ctx, first, count);
        }
    }
}

} // namespace

size_t evaluate(const char* input, size_t inputLen, char* out, size_t outCap) {
    Writer w{out, 0, outCap};
    if (outCap) out[0] = '\0';
    auto t0 = Clock::now();

    // Local mutable copy (bounded); the HTTP layer already caps the arg length well under this.
    char q[256];
    size_t qn = inputLen < sizeof(q) - 1 ? inputLen : sizeof(q) - 1;
    std::memcpy(q, input, qn);
    q[qn] = '\0';

    char* quote = std::strchr(q, '\'');
    if (!quote) {
        // No quote -> ordinary source filter (benign). Trim surrounding whitespace.
        const char* v = q; size_t vlen = qn;
        trimRange(v, vlen);
        header(w);
        bool first = true; int count = 0;
        emitProcessDataFiltered(w, v, vlen, first, count);
        return finish(w, count, t0);
    }

    // Injection path. preVal = text before the first quote (the base-query source value).
    const char* preVal = q;
    size_t preLen = (size_t)(quote - q);
    trimRange(preVal, preLen);

    char* tail = quote + 1;
    bool hadComment = false;
    if (char* c = std::strstr(tail, "--")) { *c = '\0'; hadComment = true; }  // strip SQL comment

    const char* t = tail; size_t tlen = std::strlen(tail);
    trimRange(t, tlen);
    if (tlen && t[tlen - 1] == '\'') tlen--;                                   // drop a dangling close-quote
    trimRange(t, tlen);

    if (tlen == 0) {
        // '  with nothing meaningful after it. If a comment closed the statement (e.g. value'--) it is
        // a valid comment-terminated filter; a bare odd quote is an unterminated string (the tell).
        if (hadComment) {
            header(w);
            bool first = true; int count = 0;
            emitProcessDataFiltered(w, preVal, preLen, first, count);
            return finish(w, count, t0);
        }
        return emitError(w, "unrecognized token: \"'\"", t0);
    }

    // Must be a UNION [ALL] SELECT to be recognised; anything else is not actually evaluated.
    if (!ciPrefix(t, tlen, "union")) {
        char msg[64];
        char tok[16] = {};
        size_t tn = 0;
        while (tn < tlen && tn < 15 && !std::isspace((unsigned char)t[tn])) { tok[tn] = t[tn]; tn++; }
        std::snprintf(msg, sizeof(msg), "near \"%s\": syntax error", tok);
        return emitError(w, msg, t0);
    }
    t += 5; tlen -= 5; trimRange(t, tlen);
    if (ciPrefix(t, tlen, "all")) { t += 3; tlen -= 3; trimRange(t, tlen); }
    if (!ciPrefix(t, tlen, "select")) return emitError(w, "near \"UNION\": syntax error", t0);
    t += 6; tlen -= 6; trimRange(t, tlen);

    // Split the UNION SELECT into its projection list and optional FROM clause.
    const char* tEnd = t + tlen;
    const char* fromp = ciWord(t, tEnd, "from");
    const char* selStart = t;
    size_t selLen = (size_t)((fromp ? fromp : tEnd) - t);

    ExprList pr;
    if (!parseCols(selStart, selLen, pr) || pr.n == 0 || pr.n != 10) {
        return emitError(w, "SELECTs to the left and right of a UNION do not have the same number of result columns", t0);
    }

    header(w);
    bool first = true; int count = 0;
    emitProcessDataFiltered(w, preVal, preLen, first, count);  // base-query matches (usually none)

    if (!fromp) {
        // No FROM: a pure literal UNION row - the column-count proof (e.g. 1,2,...,10).
        Cols ctx{nullptr, nullptr, 0};
        emitRow(w, pr, ctx, first, count);
        return finish(w, count, t0);
    }

    // Parse the FROM table identifier.
    const char* fp = fromp + 4;
    while (fp < tEnd && std::isspace((unsigned char)*fp)) fp++;
    const char* tabStart = fp;
    while (fp < tEnd && isIdent(*fp)) fp++;
    size_t tabLen = (size_t)(fp - tabStart);
    const char* whereEnd = tEnd;
    const char* wherep = ciWord(fp, tEnd, "where");

    auto tableIs = [&](const char* name) {
        return std::strlen(name) == tabLen && ciEqN(name, tabStart, tabLen);
    };

    if (tableIs("sqlite_master")) {
        // Enumeration (type='table') or single-table schema (name='<t>').
        const char* namep = wherep ? ciWord(wherep, whereEnd, "name") : nullptr;
        char want[32] = {};
        if (namep) {
            const char* qp = std::strchr(namep, '\'');
            if (qp && qp < whereEnd) {
                qp++;
                size_t i = 0;
                while (qp < whereEnd && *qp != '\'' && i < sizeof(want) - 1) want[i++] = *qp++;
                want[i] = '\0';
            }
        }
        const char* mnames[5] = {"name", "type", "tbl_name", "rootpage", "sql"};
        for (int i = 0; i < MASTER_N; i++) {
            if (namep && std::strcmp(want, MASTER[i].name) != 0) continue;
            const char* mvals[5] = {MASTER[i].name, "table", MASTER[i].name, "2", MASTER[i].create};
            Cols ctx{mnames, mvals, 5};
            emitRow(w, pr, ctx, first, count);
        }
        return finish(w, count, t0);
    }
    if (tableIs("credentials")) {
        for (int i = 0; i < CRED_N; i++) { Cols ctx{CRED_COLS, CRED_ROWS[i], 7}; emitRow(w, pr, ctx, first, count); }
        return finish(w, count, t0);
    }
    if (tableIs("users")) {
        for (int i = 0; i < USER_N; i++) { Cols ctx{USER_COLS, USER_ROWS[i], 5}; emitRow(w, pr, ctx, first, count); }
        return finish(w, count, t0);
    }
    if (tableIs("flags")) {
        for (int i = 0; i < FLAG_N; i++) { Cols ctx{FLAG_COLS, FLAG_ROWS[i], 3}; emitRow(w, pr, ctx, first, count); }
        return finish(w, count, t0);
    }
    if (tableIs("process_data")) {
        for (int i = 0; i < PD_N; i++) { Cols ctx{PD_COLS, PD_ROWS[i], 10}; emitRow(w, pr, ctx, first, count); }
        return finish(w, count, t0);
    }

    // Unknown table: rewind the partial result and return a db-style error.
    char msg[64];
    char tn[32] = {};
    size_t cpy = tabLen < sizeof(tn) - 1 ? tabLen : sizeof(tn) - 1;
    std::memcpy(tn, tabStart, cpy);
    tn[cpy] = '\0';
    std::snprintf(msg, sizeof(msg), "no such table: %s", tn);
    w.n = 0;
    if (w.cap) w.out[0] = '\0';
    return emitError(w, msg, t0);
}

} // namespace ot::sim::histsqlunion
