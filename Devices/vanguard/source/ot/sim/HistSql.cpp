// SPDX-License-Identifier: Apache-2.0
#include "HistSql.h"

#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ot::sim::histsql {

namespace {

// ---- bounded output writer -------------------------------------------------------------------------

struct Writer {
    char* out;
    size_t n;
    size_t cap;
};

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

// ---- case-insensitive helpers ----------------------------------------------------------------------

bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

int icmp(const char* a, const char* b) {
    while (*a && *b) {
        int d = std::tolower((unsigned char)*a) - std::tolower((unsigned char)*b);
        if (d) return d;
        a++; b++;
    }
    return std::tolower((unsigned char)*a) - std::tolower((unsigned char)*b);
}

bool parseLong(const char* s, long& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = v;
    return true;
}

// LIKE matcher: '%' matches any run (incl. empty); everything else literal; case-insensitive.
bool likeMatch(const char* text, const char* pat) {
    const char* star = nullptr;
    const char* ss = nullptr;
    while (*text) {
        if (*pat == '%') {
            star = pat++;
            ss = text;
        } else if (std::tolower((unsigned char)*pat) == std::tolower((unsigned char)*text)) {
            pat++; text++;
        } else if (star) {
            pat = star + 1;
            text = ++ss;
        } else {
            return false;
        }
    }
    while (*pat == '%') pat++;
    return *pat == '\0';
}

// ---- schema ----------------------------------------------------------------------------------------

enum Table { T_NONE = -1, T_TAGS = 0, T_SAMPLES, T_RAW, T_AUDIT };

const char* const COLS_TAGS[]    = { "name" };
const char* const COLS_SAMPLES[] = { "tag", "seq", "value", "note" };
const char* const COLS_RAW[]     = { "tag", "seq", "value", "deleted", "note" };
const char* const COLS_AUDIT[]   = { "line" };

// A column is numeric iff its name is one of these (used for numeric comparison in WHERE).
bool numericCol(const char* name) {
    return ieq(name, "seq") || ieq(name, "value") || ieq(name, "deleted");
}

const char* const TABLE_NAMES[] = { "tags", "samples", "raw_samples", "audit" };
constexpr int TABLE_COUNT = 4;

void tableCols(Table t, const char* const*& cols, int& ncols) {
    switch (t) {
        case T_TAGS:    cols = COLS_TAGS;    ncols = 1; break;
        case T_SAMPLES: cols = COLS_SAMPLES; ncols = 4; break;
        case T_RAW:     cols = COLS_RAW;     ncols = 5; break;
        case T_AUDIT:   cols = COLS_AUDIT;   ncols = 1; break;
        default:        cols = nullptr;      ncols = 0; break;
    }
}

Table findTable(const char* name) {
    for (int i = 0; i < TABLE_COUNT; i++) if (ieq(name, TABLE_NAMES[i])) return (Table)i;
    return T_NONE;
}

int findCol(Table t, const char* name) {
    const char* const* cols; int ncols;
    tableCols(t, cols, ncols);
    for (int i = 0; i < ncols; i++) if (ieq(name, cols[i])) return i;
    return -1;
}

// ---- query model + parser --------------------------------------------------------------------------

constexpr int MAX_SEL = 8;
constexpr int MAX_PRED = 4;
constexpr size_t NAME_CAP = 64;

enum Op { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_LIKE };
enum Conn { CONN_NONE, CONN_AND, CONN_OR };

struct Pred {
    int col = -1;
    Op op = OP_EQ;
    char val[NAME_CAP] = {};
};

struct Query {
    bool show = false;             // SHOW TABLES
    bool describe = false;         // DESCRIBE <table>
    Table table = T_NONE;
    bool star = false;
    bool count = false;            // SELECT COUNT(*)
    int sel[MAX_SEL];
    int nsel = 0;
    Pred preds[MAX_PRED];
    int npred = 0;
    Conn conn = CONN_NONE;
    bool hasLimit = false;
    long limit = 0;
};

struct Cursor { const char* p; const char* end; };

void skipws(Cursor& c) { while (c.p < c.end && std::isspace((unsigned char)*c.p)) c.p++; }

bool readIdent(Cursor& c, char* tok, size_t cap) {
    skipws(c);
    size_t n = 0;
    if (c.p < c.end && *c.p == '*') { c.p++; if (cap > 1) { tok[0] = '*'; tok[1] = 0; } return true; }
    while (c.p < c.end) {
        char ch = *c.p;
        if (std::isalnum((unsigned char)ch) || ch == '_' || ch == '.') {
            if (n + 1 < cap) tok[n++] = ch;
            c.p++;
        } else break;
    }
    tok[n] = 0;
    return n > 0;
}

bool readOp(Cursor& c, Op& op) {
    skipws(c);
    if (c.p >= c.end) return false;
    char a = *c.p;
    char b = (c.p + 1 < c.end) ? c.p[1] : 0;
    if (a == '=' && b == '=') { c.p += 2; op = OP_EQ; return true; }
    if (a == '=') { c.p += 1; op = OP_EQ; return true; }
    if (a == '!' && b == '=') { c.p += 2; op = OP_NE; return true; }
    if (a == '<' && b == '>') { c.p += 2; op = OP_NE; return true; }
    if (a == '<' && b == '=') { c.p += 2; op = OP_LE; return true; }
    if (a == '>' && b == '=') { c.p += 2; op = OP_GE; return true; }
    if (a == '<') { c.p += 1; op = OP_LT; return true; }
    if (a == '>') { c.p += 1; op = OP_GT; return true; }
    return false;
}

bool readValue(Cursor& c, char* tok, size_t cap) {
    skipws(c);
    if (c.p >= c.end) return false;
    size_t n = 0;
    if (*c.p == '\'') {
        c.p++;
        while (c.p < c.end && *c.p != '\'') { if (n + 1 < cap) tok[n++] = *c.p; c.p++; }
        if (c.p >= c.end) { tok[n] = 0; return false; }
        c.p++;
        tok[n] = 0;
        return true;
    }
    while (c.p < c.end) {
        char ch = *c.p;
        if (std::isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '-' || ch == '%') {
            if (n + 1 < cap) tok[n++] = ch;
            c.p++;
        } else break;
    }
    tok[n] = 0;
    return n > 0;
}

bool peekChar(Cursor& c, char want) { skipws(c); return c.p < c.end && *c.p == want; }

bool parsePred(Cursor& c, Table table, Pred& pr, char* err, size_t errCap) {
    char col[NAME_CAP];
    if (!readIdent(c, col, sizeof(col))) { std::snprintf(err, errCap, "syntax error: expected a column in WHERE"); return false; }
    pr.col = findCol(table, col);
    if (pr.col < 0) { std::snprintf(err, errCap, "no such column: %s", col); return false; }
    const char* save = c.p;
    char kw[NAME_CAP];
    Cursor probe = c;
    if (readIdent(probe, kw, sizeof(kw)) && ieq(kw, "like")) {
        c.p = probe.p;
        pr.op = OP_LIKE;
    } else {
        c.p = save;
        if (!readOp(c, pr.op)) { std::snprintf(err, errCap, "syntax error: expected a comparison operator after '%s'", col); return false; }
    }
    if (!readValue(c, pr.val, sizeof(pr.val))) { std::snprintf(err, errCap, "syntax error: expected a value in WHERE"); return false; }
    return true;
}

bool parse(const char* sql, size_t len, Query& q, char* err, size_t errCap) {
    Cursor c{ sql, sql + len };
    char tok[64];
    char selNames[MAX_SEL][NAME_CAP];

    skipws(c);
    if (c.p >= c.end) { std::snprintf(err, errCap, "empty query"); return false; }
    if (!readIdent(c, tok, sizeof(tok))) { std::snprintf(err, errCap, "syntax error: expected a statement"); return false; }

    if (ieq(tok, "show")) {
        if (!readIdent(c, tok, sizeof(tok)) || !ieq(tok, "tables")) {
            std::snprintf(err, errCap, "syntax error: only SHOW TABLES is supported"); return false;
        }
        if (peekChar(c, ';')) c.p++;
        skipws(c);
        if (c.p < c.end) { std::snprintf(err, errCap, "syntax error near end of statement"); return false; }
        q.show = true;
        return true;
    }

    if (ieq(tok, "describe") || ieq(tok, "desc")) {
        if (!readIdent(c, tok, sizeof(tok))) { std::snprintf(err, errCap, "syntax error: expected a table name after DESCRIBE"); return false; }
        q.table = findTable(tok);
        if (q.table == T_NONE) { std::snprintf(err, errCap, "no such table: %s", tok); return false; }
        if (peekChar(c, ';')) c.p++;
        skipws(c);
        if (c.p < c.end) { std::snprintf(err, errCap, "syntax error near end of statement"); return false; }
        q.describe = true;
        return true;
    }

    if (!ieq(tok, "select")) {
        std::snprintf(err, errCap, "syntax error: expected SELECT, SHOW or DESCRIBE, got '%s'", tok);
        return false;
    }

    // DISTINCT would require buffering the whole result set, which this embedded engine streams and
    // cannot afford -- reject it clearly rather than allocate a large buffer (see header note).
    const char* save = c.p;
    if (readIdent(c, tok, sizeof(tok)) && ieq(tok, "distinct")) {
        std::snprintf(err, errCap, "DISTINCT is not supported on this device (streaming engine)");
        return false;
    }
    c.p = save;

    // COUNT(*) ?
    save = c.p;
    if (readIdent(c, tok, sizeof(tok)) && ieq(tok, "count")) {
        skipws(c);
        if (peekChar(c, '(')) {
            c.p++;
            if (!peekChar(c, '*')) { std::snprintf(err, errCap, "only COUNT(*) is supported"); return false; }
            c.p++;
            if (!peekChar(c, ')')) { std::snprintf(err, errCap, "expected ')' after COUNT(*"); return false; }
            c.p++;
            q.count = true;
        } else {
            c.p = save;
        }
    } else {
        c.p = save;
    }

    if (!q.count) {
        for (;;) {
            if (!readIdent(c, tok, sizeof(tok))) { std::snprintf(err, errCap, "syntax error: expected a column or '*'"); return false; }
            if (ieq(tok, "from")) { std::snprintf(err, errCap, "syntax error: no columns before FROM"); return false; }
            if (std::strcmp(tok, "*") == 0) {
                if (q.star || q.nsel) { std::snprintf(err, errCap, "syntax error near '*'"); return false; }
                q.star = true;
            } else {
                if (q.star) { std::snprintf(err, errCap, "syntax error: '*' cannot be combined with columns"); return false; }
                if (q.nsel >= MAX_SEL) { std::snprintf(err, errCap, "too many columns in SELECT"); return false; }
                std::snprintf(selNames[q.nsel], NAME_CAP, "%s", tok);
                q.nsel++;
            }
            if (peekChar(c, ',')) { c.p++; continue; }
            break;
        }
    }

    if (!readIdent(c, tok, sizeof(tok)) || !ieq(tok, "from")) { std::snprintf(err, errCap, "syntax error: expected FROM"); return false; }
    if (!readIdent(c, tok, sizeof(tok))) { std::snprintf(err, errCap, "syntax error: expected a table name after FROM"); return false; }
    q.table = findTable(tok);
    if (q.table == T_NONE) { std::snprintf(err, errCap, "no such table: %s", tok); return false; }

    if (!q.star && !q.count) {
        for (int i = 0; i < q.nsel; i++) {
            int idx = findCol(q.table, selNames[i]);
            if (idx < 0) { std::snprintf(err, errCap, "no such column: %s", selNames[i]); return false; }
            q.sel[i] = idx;
        }
    }

    for (;;) {
        skipws(c);
        if (c.p >= c.end || *c.p == ';') break;
        const char* clauseSave = c.p;
        if (!readIdent(c, tok, sizeof(tok))) { c.p = clauseSave; break; }

        if (ieq(tok, "where")) {
            if (q.npred) { std::snprintf(err, errCap, "syntax error: duplicate WHERE"); return false; }
            for (;;) {
                if (q.npred >= MAX_PRED) { std::snprintf(err, errCap, "too many WHERE predicates (max %d)", MAX_PRED); return false; }
                if (!parsePred(c, q.table, q.preds[q.npred], err, errCap)) return false;
                q.npred++;
                const char* ps = c.p;
                char kw[NAME_CAP];
                if (readIdent(c, kw, sizeof(kw)) && (ieq(kw, "and") || ieq(kw, "or"))) {
                    Conn cc = ieq(kw, "and") ? CONN_AND : CONN_OR;
                    if (q.conn != CONN_NONE && q.conn != cc) { std::snprintf(err, errCap, "mixed AND/OR is not supported"); return false; }
                    q.conn = cc;
                    continue;
                }
                c.p = ps;
                break;
            }
        } else if (ieq(tok, "order")) {
            // ORDER BY would require buffering + sorting the whole result set; the streaming engine
            // cannot afford that on this board. Reject clearly (see header note).
            std::snprintf(err, errCap, "ORDER BY is not supported on this device (streaming engine)");
            return false;
        } else if (ieq(tok, "limit")) {
            char num[NAME_CAP];
            if (!readValue(c, num, sizeof(num))) { std::snprintf(err, errCap, "syntax error: expected a number after LIMIT"); return false; }
            long v;
            if (!parseLong(num, v) || v < 0) { std::snprintf(err, errCap, "syntax error: invalid LIMIT '%s'", num); return false; }
            q.hasLimit = true; q.limit = v;
        } else {
            std::snprintf(err, errCap, "syntax error near '%s'", tok);
            return false;
        }
    }
    if (peekChar(c, ';')) c.p++;
    skipws(c);
    if (c.p < c.end) { std::snprintf(err, errCap, "syntax error near end of statement"); return false; }
    return true;
}

// ---- one materialised row (stack-local; the engine never buffers more than one at a time) ----------

constexpr int MAX_COLS = 5;
constexpr int CELL = 56;            // widest value is the 49-char audit line (+NUL+margin)

struct Row {
    char cell[MAX_COLS][CELL];
    bool tamperDeleted;
};

// Stream every row of a table, one stack-local Row at a time, invoking fn(row). fn returns false to
// stop early (used for LIMIT). No buffer holds more than a single row -- this keeps the engine off
// both the heap and .bss, preserving the scarce internal RAM the LVGL challenge views need.
template <class F>
void forEachRow(hist::Historian& h, Table t, F fn) {
    Row r;
    if (t == T_TAGS) {
        for (int i = 0; i < h.tagCount(); i++) {
            std::memset(&r, 0, sizeof(r));
            std::snprintf(r.cell[0], CELL, "%s", h.tagName(i) ? h.tagName(i) : "");
            if (!fn(r)) return;
        }
    } else if (t == T_SAMPLES || t == T_RAW) {
        const bool raw = (t == T_RAW);
        for (int tg = 0; tg < h.tagCount(); tg++) {
            for (int i = 0; i < h.rowCount(tg); i++) {
                const hist::Row& s = h.row(tg, i);
                if (s.deleted && !raw) continue;
                std::memset(&r, 0, sizeof(r));
                std::snprintf(r.cell[0], CELL, "%s", h.tagName(tg) ? h.tagName(tg) : "");
                std::snprintf(r.cell[1], CELL, "%u", (unsigned)s.seq);
                std::snprintf(r.cell[2], CELL, "%d", (int)s.value);
                if (raw) {
                    std::snprintf(r.cell[3], CELL, "%d", s.deleted ? 1 : 0);
                    std::snprintf(r.cell[4], CELL, "%s", s.note ? s.note : "");
                    r.tamperDeleted = s.deleted && h.isTamperTag(tg);
                } else {
                    std::snprintf(r.cell[3], CELL, "%s", s.note ? s.note : "");
                }
                if (!fn(r)) return;
            }
        }
    } else if (t == T_AUDIT) {
        for (int i = 0; i < h.auditCount(); i++) {
            std::memset(&r, 0, sizeof(r));
            std::snprintf(r.cell[0], CELL, "%s", h.auditLine(i) ? h.auditLine(i) : "");
            if (!fn(r)) return;
        }
    }
}

bool evalPred(const Row& r, const Pred& p, Table t) {
    const char* const* cols; int ncols;
    tableCols(t, cols, ncols);
    const char* cv = r.cell[p.col];
    const char* vv = p.val;
    if (p.op == OP_LIKE) return likeMatch(cv, vv);
    long a, b;
    bool num = numericCol(cols[p.col]) && parseLong(cv, a) && parseLong(vv, b);
    int cmp = num ? (a < b ? -1 : (a > b ? 1 : 0)) : icmp(cv, vv);
    switch (p.op) {
        case OP_EQ: return cmp == 0;
        case OP_NE: return cmp != 0;
        case OP_LT: return cmp < 0;
        case OP_GT: return cmp > 0;
        case OP_LE: return cmp <= 0;
        case OP_GE: return cmp >= 0;
        default: return false;
    }
}

bool passWhere(const Row& r, const Query& q) {
    if (q.npred == 0) return true;
    if (q.conn == CONN_OR) {
        for (int i = 0; i < q.npred; i++) if (evalPred(r, q.preds[i], q.table)) return true;
        return false;
    }
    for (int i = 0; i < q.npred; i++) if (!evalPred(r, q.preds[i], q.table)) return false;
    return true;
}

void emitHeader(Writer& w, const Query& q) {
    const char* const* cols; int ncols;
    tableCols(q.table, cols, ncols);
    put(w, "{\"columns\":[");
    int count = q.star ? ncols : q.nsel;
    for (int i = 0; i < count; i++) {
        if (i) put(w, ",");
        putJsonStr(w, cols[q.star ? i : q.sel[i]]);
    }
    put(w, "],\"rows\":[");
}

void emitRow(Writer& w, const Query& q, const Row& r, bool first) {
    const char* const* cols; int ncols;
    tableCols(q.table, cols, ncols);
    if (!first) put(w, ",");
    put(w, "[");
    int count = q.star ? ncols : q.nsel;
    for (int i = 0; i < count; i++) {
        if (i) put(w, ",");
        putJsonStr(w, r.cell[q.star ? i : q.sel[i]]);
    }
    put(w, "]");
}

void emitError(Writer& w, Clock::time_point t0, const char* msg) {
    w.n = 0;
    put(w, "{\"error\":");
    putJsonStr(w, msg);
    put(w, ",\"rowCount\":0,\"execMs\":%u}", (unsigned)elapsedMs(t0));
}

// ---- vulnerable tag-search WHERE evaluator (for searchTag's string-concatenated query) -------------
// This is deliberately a separate, permissive evaluator from the safe parser above: it models what a
// naive, string-concatenated backend query does, so a classic SQL injection behaves realistically.

enum TokKind { TK_OPERAND, TK_OP, TK_AND, TK_OR };
// text[] kept small: the search runs on the web-server task's modest stack, and the token array is a
// stack local, so a fat Tok (and a large toks[]) can overflow that stack. 40 bytes holds any tag name
// or literal we compare.
struct Tok { TokKind kind; bool quoted; Op op; char text[40]; };

// Tokenise a WHERE string up to a '--' comment (which ends the clause, SQL-style). Bounded.
int tokenizeWhere(const char* s, Tok* toks, int maxToks) {
    int n = 0;
    const char* p = s;
    while (*p && n < maxToks) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (p[0] == '-' && p[1] == '-') break;               // SQL line comment: ignore the rest
        if (*p == '\'') {                                    // quoted string literal
            p++;
            Tok& t = toks[n]; t.kind = TK_OPERAND; t.quoted = true;
            size_t k = 0;
            while (*p && *p != '\'' && k + 1 < sizeof(t.text)) t.text[k++] = *p++;
            t.text[k] = 0;
            if (*p == '\'') p++;                             // consume closing quote if present
            n++; continue;
        }
        if (*p == '=' || *p == '!' || *p == '<' || *p == '>') {  // comparison operator
            Tok& t = toks[n]; t.kind = TK_OP; t.quoted = false; t.text[0] = 0;
            char a = p[0], b = p[1];
            if (a == '=' && b == '=') { t.op = OP_EQ; p += 2; }
            else if (a == '=')        { t.op = OP_EQ; p += 1; }
            else if (a == '!' && b == '=') { t.op = OP_NE; p += 2; }
            else if (a == '<' && b == '>') { t.op = OP_NE; p += 2; }
            else if (a == '<' && b == '=') { t.op = OP_LE; p += 2; }
            else if (a == '>' && b == '=') { t.op = OP_GE; p += 2; }
            else if (a == '<')        { t.op = OP_LT; p += 1; }
            else                      { t.op = OP_GT; p += 1; }
            n++; continue;
        }
        // bareword / number / keyword (AND/OR/column name/literal)
        Tok& t = toks[n]; t.quoted = false;
        size_t k = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\'' &&
               *p != '=' && *p != '!' && *p != '<' && *p != '>' && k + 1 < sizeof(t.text)) {
            t.text[k++] = *p++;
        }
        t.text[k] = 0;
        t.kind = ieq(t.text, "and") ? TK_AND : ieq(t.text, "or") ? TK_OR : TK_OPERAND;
        n++;
    }
    return n;
}

// Resolve an operand to a string: an unquoted raw_samples column name -> that row's cell; anything
// else (a quoted literal, a number, an unknown bareword) -> its own text. Row cells for T_RAW are
// [0]=tag [1]=seq [2]=value [3]=deleted [4]=note.
void resolveOperand(const Tok& t, const Row& r, char* out, size_t cap) {
    if (!t.quoted) {
        if (ieq(t.text, "tag"))     { std::snprintf(out, cap, "%s", r.cell[0]); return; }
        if (ieq(t.text, "seq"))     { std::snprintf(out, cap, "%s", r.cell[1]); return; }
        if (ieq(t.text, "value"))   { std::snprintf(out, cap, "%s", r.cell[2]); return; }
        if (ieq(t.text, "deleted")) { std::snprintf(out, cap, "%s", r.cell[3]); return; }
    }
    std::snprintf(out, cap, "%s", t.text);
}

bool evalCmp(const Tok& l, const Tok& opT, const Tok& rt, const Row& r) {
    char lv[64], rv[64];
    resolveOperand(l, r, lv, sizeof(lv));
    resolveOperand(rt, r, rv, sizeof(rv));
    long a, b;
    bool num = parseLong(lv, a) && parseLong(rv, b);
    int cmp = num ? (a < b ? -1 : (a > b ? 1 : 0)) : icmp(lv, rv);
    switch (opT.op) {
        case OP_EQ: return cmp == 0;
        case OP_NE: return cmp != 0;
        case OP_LT: return cmp < 0;
        case OP_GT: return cmp > 0;
        case OP_LE: return cmp <= 0;
        case OP_GE: return cmp >= 0;
        default: return false;
    }
}

// Evaluate (operand op operand) terms joined by AND/OR, left to right (no precedence). A tail that is
// not a well-formed comparison contributes a false term. Empty clause -> true.
bool evalVulnWhere(const Tok* toks, int n, const Row& r) {
    if (n == 0) return true;
    bool acc = false, haveAcc = false;
    Conn pending = CONN_AND;
    int i = 0;
    while (i < n) {
        bool term = false;
        if (i + 2 < n && toks[i].kind == TK_OPERAND && toks[i + 1].kind == TK_OP && toks[i + 2].kind == TK_OPERAND) {
            term = evalCmp(toks[i], toks[i + 1], toks[i + 2], r);
            i += 3;
        } else {
            i += 1; // skip a stray token; term stays false
        }
        if (!haveAcc) { acc = term; haveAcc = true; }
        else if (pending == CONN_AND) acc = acc && term;
        else acc = acc || term;
        if (i < n && (toks[i].kind == TK_AND || toks[i].kind == TK_OR)) {
            pending = (toks[i].kind == TK_AND) ? CONN_AND : CONN_OR;
            i++;
        } else {
            break;
        }
    }
    return acc;
}

} // namespace

size_t execute(hist::Historian& h, const char* sql, size_t sqlLen, char* out, size_t outCap) {
    Clock::time_point t0 = Clock::now();
    Writer w{ out, 0, outCap };
    if (outCap == 0) return 0;

    Query q;
    char err[96];
    if (!parse(sql, sqlLen, q, err, sizeof(err))) { emitError(w, t0, err); return w.n; }

    if (q.show) {
        put(w, "{\"columns\":[\"name\"],\"rows\":[");
        for (int i = 0; i < TABLE_COUNT; i++) { if (i) put(w, ","); put(w, "["); putJsonStr(w, TABLE_NAMES[i]); put(w, "]"); }
        put(w, "],\"rowCount\":%d,\"execMs\":%u}", TABLE_COUNT, (unsigned)elapsedMs(t0));
        return w.n;
    }

    if (q.describe) {
        const char* const* cols; int ncols;
        tableCols(q.table, cols, ncols);
        put(w, "{\"columns\":[\"column\",\"type\"],\"rows\":[");
        for (int i = 0; i < ncols; i++) {
            if (i) put(w, ",");
            put(w, "["); putJsonStr(w, cols[i]); put(w, ",");
            putJsonStr(w, numericCol(cols[i]) ? "integer" : "text"); put(w, "]");
        }
        put(w, "],\"rowCount\":%d,\"execMs\":%u}", ncols, (unsigned)elapsedMs(t0));
        return w.n;
    }

    if (q.count) {
        // COUNT(*) returns only a number -- it never retrieves the purged record, so it cannot solve.
        long cnt = 0;
        forEachRow(h, q.table, [&](const Row& r) { if (passWhere(r, q)) cnt++; return true; });
        put(w, "{\"columns\":[\"count\"],\"rows\":[[\"%ld\"]],\"rowCount\":1,\"execMs\":%u}",
            cnt, (unsigned)elapsedMs(t0));
        return w.n;
    }

    emitHeader(w, q);
    long emitted = 0;
    bool recovered = false;
    forEachRow(h, q.table, [&](const Row& r) -> bool {
        if (!passWhere(r, q)) return true;
        if (q.hasLimit && emitted >= q.limit) return false;   // LIMIT reached: stop streaming
        emitRow(w, q, r, emitted == 0);
        if (r.tamperDeleted) recovered = true;                // an actual record row was returned
        emitted++;
        return !(q.hasLimit && emitted >= q.limit);
    });
    put(w, "],\"rowCount\":%ld,\"execMs\":%u}", emitted, (unsigned)elapsedMs(t0));

    // Completion latches ONLY when the tamper tag's purged row was actually returned to the player.
    if (recovered) h.reveal();
    return w.n;
}

size_t searchTag(hist::Historian& h, const char* userInput, size_t inputLen, char* out, size_t outCap) {
    Clock::time_point t0 = Clock::now();
    Writer w{ out, 0, outCap };
    if (outCap == 0) return 0;

    // THE VULNERABILITY: the raw user input is concatenated straight into the backend query instead of
    // being parameterised. The trailing scope guard "AND deleted = 0" is what the UI relies on to hide
    // the purged incident-window rows; an injection that neutralises it escapes that scope.
    char inbuf[128];
    size_t k = 0;
    for (size_t i = 0; i < inputLen && k + 1 < sizeof(inbuf); i++) inbuf[k++] = userInput[i];
    inbuf[k] = 0;
    char where[200];
    std::snprintf(where, sizeof(where), "tag = '%s' AND deleted = 0", inbuf);

    // Bounded token array kept small to stay within the web-server task's stack (see Tok note above).
    Tok toks[20];
    int ntok = tokenizeWhere(where, toks, 20);

    put(w, "{\"columns\":[\"tag\",\"seq\",\"value\",\"note\"],\"rows\":[");
    long emitted = 0;
    bool recovered = false;
    forEachRow(h, T_RAW, [&](const Row& r) -> bool {
        if (!evalVulnWhere(toks, ntok, r)) return true;
        if (emitted) put(w, ",");
        put(w, "[");
        putJsonStr(w, r.cell[0]); put(w, ","); putJsonStr(w, r.cell[1]); put(w, ",");
        putJsonStr(w, r.cell[2]); put(w, ","); putJsonStr(w, r.cell[4]);
        put(w, "]");
        if (r.tamperDeleted) recovered = true;  // a purged tamper row escaped the scope -> recovered
        emitted++;
        return true;
    });
    put(w, "],\"rowCount\":%ld,\"execMs\":%u}", emitted, (unsigned)elapsedMs(t0));

    // Same solve as RAW: latches only when the tamper tag's purged row was actually returned.
    if (recovered) h.reveal();
    return w.n;
}

} // namespace ot::sim::histsql
