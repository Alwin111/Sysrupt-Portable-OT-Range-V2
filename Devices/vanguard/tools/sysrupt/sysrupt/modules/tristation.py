# SPDX-License-Identifier: Apache-2.0
"""TriStation-lite module -- Safety Instrumented Bypass (Challenge 02), target port 1502.

Frame formats are ported from the repository reference client ``tools/solve/ch02_sis.py``:

    READ      AA 55 01 rH rL                -> AA 55 01 rH rL vH vL   (7 bytes)
    WRITE     AA 55 02 rH rL vH vL          -> status byte at [3]     (4 bytes; 0 ok, 1 rejected)
    DOWNLOAD  AA 55 10 fH fL kH kL trip     -> status byte at [3]     (0 accepted, 1 locked, 2 bad fw)

The simulator handles one request per connection, so each operation uses a fresh connection. The
player supplies the firmware id, debug key and trip flag explicitly -- the module never fills them
in, and a WRITE/DOWNLOAD acknowledgement is never reported as a solved challenge.
"""
from __future__ import annotations

import time

from ..registry import ArgSpec, Command
from ..result import OperationResult, Verify
from ..transport import TransportError
from ..util import find_flag, hexdump

SYNC = bytes([0xAA, 0x55])
DEFAULT_FLAG_BASE = 100     # documented flag object block (location, not the solution)
FLAG_LEN = 16


def _oneshot(ctx, payload: bytes, want: int) -> bytes:
    tcp = ctx.new_tcp("tristation")
    try:
        tcp.send(payload)
        return tcp.recv_exact(want)
    finally:
        tcp.close()


def read_reg(ctx, reg: int):
    resp = _oneshot(ctx, SYNC + bytes([0x01, reg >> 8, reg & 0xFF]), 7)
    if len(resp) == 7:
        return int.from_bytes(resp[5:7], "big"), resp
    return None, resp


def write_reg(ctx, reg: int, val: int):
    resp = _oneshot(ctx, SYNC + bytes([0x02, reg >> 8, reg & 0xFF, val >> 8, val & 0xFF]), 4)
    return (resp[3] if len(resp) == 4 else None), resp


def download(ctx, fw: int, key: int, trip: bool):
    payload = SYNC + bytes([0x10, fw >> 8, fw & 0xFF, key >> 8, key & 0xFF, 1 if trip else 0])
    resp = _oneshot(ctx, payload, 4)
    return (resp[3] if len(resp) == 4 else None), resp


# ---- handlers ----

def h_read(ctx, ns):
    t0 = time.monotonic()
    v, resp = read_reg(ctx, ns["reg"])
    res = OperationResult("TriStation", "read", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"READ reg {ns['reg']}", response=hexdump(resp),
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    if v is None:
        res.verify = Verify.SENT
        res.detail = "no/short response"
    else:
        res.verify = Verify.ACKED
        res.response = f"reg {ns['reg']} = {v} (0x{v:04x})"
    return res


def h_write(ctx, ns):
    t0 = time.monotonic()
    status, resp = write_reg(ctx, ns["reg"], ns["value"])
    res = OperationResult("TriStation", "write", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"WRITE reg {ns['reg']} := {ns['value']}", response=hexdump(resp),
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    if status == 0:
        res.verify = Verify.ACKED
        res.detail = "write acknowledged (status 0) -- not proof of challenge completion"
    elif status == 1:
        res.verify = Verify.SENT
        res.detail = "write REJECTED (status 1) -- e.g. a safety interlock blocks a direct write"
    else:
        res.verify = Verify.SENT
        res.detail = f"unexpected status {status}"
    return res


def h_download(ctx, ns):
    t0 = time.monotonic()
    status, resp = download(ctx, ns["fw"], ns["key"], bool(ns["trip"]))
    res = OperationResult("TriStation", "download (program)", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"DOWNLOAD fw={ns['fw']} key=0x{ns['key']:04x} trip={ns['trip']}",
                          response=hexdump(resp), elapsed_ms=(time.monotonic() - t0) * 1000.0)
    meaning = {0: "accepted", 1: "locked (open the download lock first)", 2: "bad firmware id"}
    if status == 0:
        res.verify = Verify.ACKED
        res.detail = ("download accepted -- hold the new program and read the flag block to confirm "
                      "completion ('tristation flag'); acceptance alone is not completion")
    else:
        res.verify = Verify.SENT
        res.detail = f"download not accepted: {meaning.get(status, status)}"
    return res


def _snapshot(ctx, start, count) -> dict:
    """Read a register range into {reg: value_or_None}. An unreadable register is None, NEVER 0."""
    snap = {}
    for r in range(start, start + count):
        try:
            v, _ = read_reg(ctx, r)
        except TransportError:
            v = None               # a transport failure -> unreadable (None), never 0
        snap[r] = v
    return snap


def _dump_watch(ctx, start, count, interval: float):
    """Two snapshots `interval` seconds apart, compared by register address. Read-only."""
    interval = min(max(interval, 0.2), 30.0)
    t0 = time.monotonic()
    snap1 = _snapshot(ctx, start, count)
    time.sleep(interval)
    snap2 = _snapshot(ctx, start, count)
    changed, appeared, disappeared, unreadable = [], [], [], []
    for r in range(start, start + count):
        a, b = snap1.get(r), snap2.get(r)
        if a is None and b is None:
            unreadable.append(r)            # missing in BOTH -- shown as unreadable, never 0
        elif a is None and b is not None:
            appeared.append((r, b))         # unreadable -> readable
        elif a is not None and b is None:
            disappeared.append((r, a))      # readable -> unreadable
        elif a != b:
            changed.append((r, a, b))
    rows = [
        "  changed:      " + (", ".join(f"reg {r}: {a} -> {b}" for r, a, b in changed) or "(none)"),
        "  appeared:     " + (", ".join(f"reg {r}: (no resp) -> {b}" for r, b in appeared) or "(none)"),
        "  disappeared:  " + (", ".join(f"reg {r}: {a} -> (no resp)" for r, a in disappeared) or "(none)"),
    ]
    if unreadable:
        rows.append("  unreadable:   " + ", ".join(f"reg {r}" for r in unreadable)
                    + "  (no response in EITHER snapshot -- not treated as 0)")
    res = OperationResult("TriStation", "dump (watch)", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"two snapshots of regs {start}..{start + count - 1}, {interval}s apart",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    res.verify = Verify.ACKED
    res.response = f"{len(changed)} changed, {len(appeared)} appeared, {len(disappeared)} disappeared"
    res.extra["diff"] = "\n" + "\n".join(rows)
    res.detail = "read-only; compared by register address. Missing data is shown as unreadable, never 0."
    return res


def h_dump(ctx, ns):
    start, count = ns["start"], ns["count"]
    if count < 1 or count > 32:
        raise ValueError("count must be 1..32")
    if ns.get("watch"):
        return _dump_watch(ctx, start, count, ns.get("interval") or 1.0)
    if ns.get("diff"):
        raise ValueError("--diff requires --watch (there is only one snapshot without it)")
    t0 = time.monotonic()
    rows = []
    for r in range(start, start + count):
        v, _ = read_reg(ctx, r)
        rows.append(f"  reg {r}: {v if v is not None else '(no resp)'}")
    res = OperationResult("TriStation", "dump", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"READ regs {start}..{start + count - 1}",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    res.verify = Verify.ACKED
    res.response = f"{count} registers"
    res.extra["dump"] = "\n" + "\n".join(rows)
    return res


def h_verify(ctx, ns):
    t0 = time.monotonic()
    v, _ = read_reg(ctx, ns["reg"])
    res = OperationResult("TriStation", "verify", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"reg {ns['reg']} == {ns['expected']}?",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    res.response = f"read {v}"
    if v == ns["expected"]:
        res.verify = Verify.STATE_CHANGED
        res.detail = "observed value matches expected (state confirmed; not necessarily completion)"
    else:
        res.verify = Verify.ACKED
        res.detail = "value does not match expected"
    return res


def h_flag(ctx, ns):
    base = ns.get("base") or DEFAULT_FLAG_BASE
    t0 = time.monotonic()
    text = ""
    for i in range(FLAG_LEN):
        v, _ = read_reg(ctx, base + i)
        if v is None:
            break
        for c in (v >> 8, v & 0xFF):
            if c:
                text += chr(c)
    flag = find_flag(text)
    res = OperationResult("TriStation", "flag", f"{ctx.target}:{ctx.port('tristation')}",
                          request=f"read flag block reg {base}..{base + FLAG_LEN - 1}",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    if flag:
        res.verify = Verify.COMPLETED
        res.flag = flag
        res.response = "flag block populated"
        res.detail = "the SIS exposed the flag -- completion condition met"
    else:
        res.verify = Verify.ACKED
        res.response = "flag block empty"
        res.detail = "not solved yet (keep the trip disabled + firmware matched and hold)"
    return res


def register(reg):
    reg.add_group("tristation", "TriStation-lite (Safety Instrumented Bypass, Ch02)")
    reg.add(Command("read", "Read a safety register", h_read, group="tristation",
                    args=[ArgSpec("reg", "register number", type="int")],
                    examples=["tristation read 0", "tristation read 42"]))
    reg.add(Command("write", "Write a safety register -- STATE CHANGING", h_write, group="tristation",
                    modifies_state=True,
                    args=[ArgSpec("reg", "register", type="int"), ArgSpec("value", "value", type="int")],
                    examples=["tristation write 1 1"]))
    reg.add(Command("download", "Program download (firmware + key + trip flag) -- STATE CHANGING",
                    h_download, group="tristation", modifies_state=True,
                    args=[ArgSpec("--fw", "firmware id", required=True, type="int"),
                          ArgSpec("--key", "debug key (read it first)", required=True, type="int"),
                          ArgSpec("--trip", "trip function 1=on 0=off", required=True, type="int")],
                    examples=["tristation download --fw 3008 --key 0x1a2b --trip 0"],
                    long_help="You must supply the firmware id, debug key and trip flag yourself. "
                              "The debug key rolls over time -- read it right before downloading."))
    reg.add(Command("dump", "Read a range of registers (--watch --diff for a two-snapshot diff)", h_dump,
                    group="tristation",
                    args=[ArgSpec("start", "start register", type="int"),
                          ArgSpec("count", "count (1..32)", type="int"),
                          ArgSpec("--watch", "take two snapshots and show what changed", required=False,
                                  is_flag=True, default=False),
                          ArgSpec("--diff", "show the diff view (requires --watch)", required=False,
                                  is_flag=True, default=False),
                          ArgSpec("--interval", "seconds between the two snapshots (0.2..30)",
                                  required=False, type="float", default=1.0)],
                    examples=["tristation dump 0 8", "tristation dump 0 8 --watch --diff --interval 2"],
                    long_help="Without --watch this is the plain register dump. With --watch it captures two "
                              "snapshots an interval apart and reports changed / appeared / disappeared "
                              "registers (old -> new). Read-only; a register with no response is shown as "
                              "unreadable, never as 0."))
    reg.add(Command("verify", "Read a register and compare to an expected value", h_verify,
                    group="tristation",
                    args=[ArgSpec("reg", "register", type="int"),
                          ArgSpec("expected", "expected value", type="int")],
                    examples=["tristation verify 0 0"]))
    reg.add(Command("flag", "Read the flag block (if the SIS exposes it)", h_flag, group="tristation",
                    args=[ArgSpec("--base", "flag block base register", required=False, type="int",
                                  default=DEFAULT_FLAG_BASE)],
                    examples=["tristation flag"]))
