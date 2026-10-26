// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

// ====================================================================================================
// OFFENSIVE HISTORIAN SQL-INJECTION SIMULATOR  (unscored, self-directed discovery exercise)
// ====================================================================================================
//
// This reproduces the LEARNING EXPERIENCE of the V1 Sysrupt OT Range "Pivot to OT" historian step
// (services/historian/app/server.py), where the `Filter by Source` field concatenates user input
// straight into  SELECT * FROM process_data WHERE source = '<input>'  and is therefore injectable.
//
// *** THIS IS A BOUNDED PATTERN-MATCHING SIMULATOR. IT IS **NOT** SQLite, AND NOT A SQL ENGINE. ***
// It recognises the specific V1 injection SHAPES below against a small static in-flash dataset and
// returns canned, realistic-looking results. It does not parse or execute arbitrary SQL. Payloads that
// do not match a recognised shape return a generic error - they are NOT really evaluated.
//
// It is DELIBERATELY SEPARATE from sim/HistSql (the read-only *forensic* engine behind defensive
// challenges #07 Historian Forensics and #09 Trojan Historian): this module shares no state with
// ot::hist::Historian, never calls reveal(), and cannot affect any scored challenge. It is reached via
// the additive  GET /hist/analyze?source=<input>  route and its own "PROCESS DATA - Filter by Source"
// panel; nothing here is wired to the badge scoreboard.
//
// ---- Recognised injection shapes (the intended V1 discovery flow) ---------------------------------
//   <plain value>                               -> process_data rows whose source == value (benign)
//   <value>'--  (or any comment-terminated '')  -> benign filter for <value> (the '--' closes cleanly)
//   '                                           -> unterminated-string error  (the injectability tell)
//   ...' UNION SELECT <N exprs> [FROM t [WHERE]] [--]
//       N != 10                                 -> "...different number of result columns" error
//       N == 10, no FROM                        -> one row of the literal expressions (col-count proof)
//       FROM sqlite_master WHERE type='table'   -> the table names (users, process_data, credentials, flags)
//       FROM sqlite_master WHERE name='<t>'     -> that table's CREATE TABLE text (schema inspection)
//       FROM credentials                        -> the 6 stored credential records
//       FROM users                              -> the 2 user records
//       FROM flags                              -> the flag row  flag{sql1_1n_th3_h1st0r14n}
//       FROM process_data                       -> the process_data sample rows
//       FROM <unknown>                           -> "no such table" error
//
// Projection expressions understood inside a UNION SELECT: the target table's column names, the
// concatenation idiom  a||'sep'||b , the  replace(sql,char(10),' ')  schema idiom (treated as `sql`),
// and quoted/numeric literals (echoed verbatim). The result's column headers are always the 10
// process_data columns, exactly as real SQLite would name them from the first SELECT (SELECT *).
//
// ---- Explicitly NOT reproduced --------------------------------------------------------------------
//   * Real SQL parsing/execution, data types, expressions, functions (beyond the two idioms above).
//   * Boolean/OR tautologies (' OR 1=1 --), blind/time-based injection, stacked queries, subqueries.
//   * INSERT/UPDATE/DELETE or any write. ORDER BY / GROUP BY / JOIN / DISTINCT.
//   * The downstream V1 pivot (SSH jump host, SSH tunnelling, SCADA HMI) - out of scope.
//
// ---- Output ---------------------------------------------------------------------------------------
// A small, bounded JSON object written into `out` (never overflows; always NUL-terminated), the same
// shape the /hist SQL console already uses:
//   {"columns":[...10...],"rows":[["v",...],...],"rowCount":N,"execMs":M}
//   {"error":"<sqlite-style message>","rowCount":0,"execMs":M}
// The flag is returned ONLY by a correctly-formed  UNION ... FROM flags  query; it is never emitted by
// any other query and is never shown by the interface on its own.
namespace ot::sim::histsqlunion {

// The authoritative flag hidden in the simulated `flags` table and recovered via UNION-based SQL
// injection. This is the SINGLE source of truth: the simulator emits exactly this string, and the
// scored Defensive #07 (Historian SQL Injection) validates a submission against it (ot::def::
// FORENSICS_FLAG aliases this), so the registry, the simulator and the submission handler cannot drift.
constexpr const char* FLAG = "flag{sql1_1n_th3_h1st0r14n}";

// Evaluate the Filter-by-Source value `input` (the raw text the player typed). `inputLen` bounds how
// much is read. Returns the number of bytes written to `out` (excluding the NUL). No heap is allocated
// here; the dataset is static. Pure/stateless - safe to call without any lock.
size_t evaluate(const char* input, size_t inputLen, char* out, size_t outCap);

} // namespace ot::sim::histsqlunion
