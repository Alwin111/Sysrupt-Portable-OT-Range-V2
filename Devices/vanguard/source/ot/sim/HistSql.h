// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Historian.h"

#include <cstddef>

// A tiny, read-only SQL subset over the Historian Forensics model (the "Historian" challenge). It is a
// web-facing VIEW onto the SAME ot::hist::Historian that the :9000 text query protocol (net/HistProto)
// uses, so recovering the tampered tag's soft-deleted ("purged") row through this interface latches the
// identical solve condition (hist::Historian::reveal()) - the challenge completion logic is unchanged.
//
// Supported grammar (keywords case-insensitive; one statement; a trailing ';' is optional):
//   SHOW TABLES
//   DESCRIBE <table>                        (schema inspection; DESC is accepted too)
//   SELECT <*|col[,col]...> FROM <table>
//          [WHERE <pred> [ (AND|OR) <pred> ]... ]   (up to 4 predicates, one connective kind)
//          [LIMIT <n>]
//   SELECT COUNT(*) FROM <table> [WHERE ...]
// where <pred> is  <col> <op> <'string'|number>   with op in  =  !=  <>  <  >  <=  >=  LIKE
// LIKE uses '%' as the wildcard. No subqueries, no expressions.
//
// The engine STREAMS one row at a time and never buffers the result set, so it uses no heap or .bss
// for rows -- critical on this no-PSRAM board, where a result buffer would steal the scarce internal
// RAM the LVGL challenge views need and crash them on screen open. Consequently the two operations
// that require holding all rows are NOT supported and are rejected with a clear error:
//   - ORDER BY  (would need to buffer + sort every row)
//   - DISTINCT  (would need to remember every emitted row)
//
// JOIN is likewise not implemented: the only cross-table relationship players need is audit->sample,
// but `audit` is free text with no relational key column, so a JOIN would have to pretend text fields
// are columns. Instead, read the audit breadcrumb (the purged seq) and query raw_samples with a WHERE
// on that seq -- the intended forensic correlation path.
//
// Tables (discover them with SHOW TABLES - nothing is revealed just by opening the console):
//   tags(name)                               -- the logged tags
//   samples(tag,seq,value,note)              -- CLEAN view: soft-deleted rows are hidden
//   raw_samples(tag,seq,value,deleted,note)  -- RAW view: includes the purged rows (forensic recovery)
//   audit(line)                              -- the audit trail (points at the PURGED seq/tag)
//
// The response is a small, bounded JSON object written into `out` (never overflows; always NUL-terminated):
//   {"columns":["c",...],"rows":[["v",...],...],"rowCount":N,"execMs":M}
//   {"error":"<sqlite-style message>","rowCount":0,"execMs":M}
// Only the rows a query actually returns are emitted; the flag is never emitted by the clean `samples`
// view and never returned merely because the console was opened.
namespace ot::sim::histsql {

// Returns the number of bytes written to `out` (excluding the NUL). `sqlLen` bounds how much of `sql`
// is read. Recovering the tamper tag's deleted row calls h.reveal(); all other queries are read-only.
size_t execute(hist::Historian& h, const char* sql, size_t sqlLen, char* out, size_t outCap);

// Vulnerable "tag search": the historian console's tag-filter concatenates the raw user input directly
// into a backend query (effectively  tag = '<input>' AND deleted = 0 ) instead of parameterising it.
// Normal input returns only the committed samples for that tag; the trailing "AND deleted = 0" scope
// guard hides the purged "incident window" rows. A classic SQL injection that neutralises that guard
// (closing the quote, a tautology, and a '--' comment) escapes the intended scope and returns the
// hidden purged rows - recovering the tamper record, which latches reveal() (the same solve as RAW).
// A non-tautological injection returns nothing. Output is the same bounded JSON shape as execute():
//   {"columns":["tag","seq","value","note"],"rows":[...],"rowCount":N,"execMs":M}
size_t searchTag(hist::Historian& h, const char* userInput, size_t inputLen, char* out, size_t outCap);

} // namespace ot::sim::histsql
