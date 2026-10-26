# SPDX-License-Identifier: Apache-2.0
"""IEC 60870-5-104 module -- Tripping Breaker (Challenge 01), target port 2404.

Frame handling is ported from the repository reference client ``tools/solve/ch01_iec104.py``.
Read-only investigation (status / interrogate / read / flag) never sends a control command. The
only state-changing operation is ``iec104 command``, which requires the player to supply the IOA,
value and originator explicitly -- the module never picks them. Point meanings are NOT inferred
from address numbers; the interrogation simply lists what the RTU returns.
"""
from __future__ import annotations

import time

from ..registry import ArgSpec, Command
from ..result import OperationResult, Verify
from ..transport import TransportError
from ..util import find_flag, hexdump

CA = 1                 # common address (ASDU) used by the simulator
WAIT_MAX_S = 300       # bounded wait-until timeout
WAIT_MAX_ERRORS = 3    # consecutive transport errors tolerated before giving up (no infinite retry)
FLAG_BASE = 5000       # the flag object block (player-visible: the interrogation returns it)
FLAG_LEN = 16


def startdt() -> bytes:
    return bytes([0x68, 0x04, 0x07, 0, 0, 0])


def client_i(typ: int, cot: int, org: int, ca: int, ioa: int, ie: bytes) -> bytes:
    b = bytearray([0x68, 0x00, 0, 0, 0, 0,
                   typ, 0x01, cot & 0x3F, org, ca & 0xFF, (ca >> 8) & 0xFF,
                   ioa & 0xFF, (ioa >> 8) & 0xFF, (ioa >> 16) & 0xFF])
    b += bytes(ie)
    b[1] = len(b) - 2
    return bytes(b)


def iter_iframes(buf: bytes):
    i = 0
    while i + 2 <= len(buf) and buf[i] == 0x68:
        alen = buf[i + 1] + 2
        if i + alen > len(buf):
            break
        if (buf[i + 2] & 1) == 0:          # I-frame (lowest control bit clear)
            yield buf[i + 6:i + alen]      # the ASDU
        i += alen


def point_value(asdu: bytes):
    t = asdu[0]
    if t == 1:                              # M_SP single point
        return asdu[9] & 1
    return int.from_bytes(asdu[9:11], "little", signed=True)


def find_ioa(buf: bytes, ioa: int):
    for asdu in iter_iframes(buf):
        a = asdu[6] | (asdu[7] << 8) | (asdu[8] << 16)
        if a == ioa:
            return point_value(asdu)
    return None


def all_points(buf: bytes):
    pts = []
    for asdu in iter_iframes(buf):
        a = asdu[6] | (asdu[7] << 8) | (asdu[8] << 16)
        pts.append((a, asdu[0], point_value(asdu)))
    return pts


def has_negative_confirm(buf: bytes) -> bool:
    """True if any I-frame is an activation confirmation (COT 7) with the P/N (negative) bit set."""
    for asdu in iter_iframes(buf):
        if (asdu[2] & 0x3F) == 7 and (asdu[2] & 0x40):
            return True
    return False


def has_positive_actcon(buf: bytes) -> bool:
    for asdu in iter_iframes(buf):
        if (asdu[2] & 0x3F) == 7 and not (asdu[2] & 0x40):
            return True
    return False


def _startdt_on(tcp) -> bytes:
    tcp.send(startdt())
    return tcp.recv_exact(6)


def _interrogate(tcp) -> bytes:
    tcp.send(client_i(100, 6, 0, CA, 0, bytes([20])))   # C_IC general interrogation, QOI=20
    return tcp.drain()


# ---- handlers ----

def h_status(ctx, ns):
    tcp = ctx.tcp("iec104")
    t0 = time.monotonic()
    con = _startdt_on(tcp)
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("IEC-104", "status", f"{ctx.target}:{ctx.port('iec104')}",
                          request="STARTDT", response=hexdump(con), elapsed_ms=elapsed)
    if len(con) >= 3 and con[2] == 0x0B:
        res.verify = Verify.ACKED
        res.detail = "STARTDT_CON received (data transfer started)"
    else:
        res.verify = Verify.SENT
        res.detail = "no STARTDT_CON"
    return res


def h_interrogate(ctx, ns):
    tcp = ctx.tcp("iec104")
    t0 = time.monotonic()
    _startdt_on(tcp)
    raw = _interrogate(tcp)
    elapsed = (time.monotonic() - t0) * 1000.0
    pts = all_points(raw)
    res = OperationResult("IEC-104", "interrogate", f"{ctx.target}:{ctx.port('iec104')}",
                          request="C_IC general interrogation (QOI=20)", elapsed_ms=elapsed)
    res.verify = Verify.ACKED if pts else Verify.SENT
    res.response = f"{len(pts)} monitored points"
    rows = [f"  IOA {a:<5} type={t:<3} value={v}" for a, t, v in pts]
    res.extra["points"] = "\n" + "\n".join(rows) if rows else "(none)"
    res.detail = "read-only: point meanings are not inferred from IOA numbers -- consult the evidence"
    return res


def h_read(ctx, ns):
    tcp = ctx.tcp("iec104")
    t0 = time.monotonic()
    _startdt_on(tcp)
    raw = _interrogate(tcp)
    elapsed = (time.monotonic() - t0) * 1000.0
    v = find_ioa(raw, ns["ioa"])
    res = OperationResult("IEC-104", "read", f"{ctx.target}:{ctx.port('iec104')}",
                          request=f"read IOA {ns['ioa']} from interrogation", elapsed_ms=elapsed)
    res.verify = Verify.ACKED if v is not None else Verify.SENT
    res.response = f"IOA {ns['ioa']} = {v}" if v is not None else f"IOA {ns['ioa']} not present"
    return res


def h_command(ctx, ns):
    """Explicit single command (C_SC). STATE CHANGING. The player supplies IOA, value, originator."""
    ioa, value, org = ns["ioa"], ns["value"], ns["originator"]
    if value not in (0, 1):
        raise ValueError("value (SCS) must be 0 or 1")
    frame = client_i(45, 6, org, CA, ioa, bytes([value & 0x01]))
    tcp = ctx.tcp("iec104")
    t0 = time.monotonic()
    _startdt_on(tcp)
    tcp.send(frame)
    resp = tcp.drain()
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("IEC-104", "command (C_SC)", f"{ctx.target}:{ctx.port('iec104')}",
                          request=f"C_SC IOA={ioa} SCS={value} originator={org}  frame={hexdump(frame)}",
                          response=hexdump(resp) or "(none)", elapsed_ms=elapsed)
    if has_negative_confirm(resp):
        res.verify = Verify.SENT
        res.detail = "NEGATIVE activation confirmation -- the RTU rejected the command"
    elif has_positive_actcon(resp):
        res.verify = Verify.ACKED
        res.detail = ("POSITIVE ACTCON -- acknowledgement only. This does NOT prove the breaker "
                      "moved. Re-interrogate the point (iec104 read / interrogate) or run "
                      "'iec104 flag' to confirm an actual state change / completion.")
    else:
        res.verify = Verify.SENT
        res.detail = "no activation confirmation seen in the reply window"
    return res


def h_flag(ctx, ns):
    """Re-interrogate and read the flag object block -- completion is driven by the RTU's own output."""
    tcp = ctx.tcp("iec104")
    t0 = time.monotonic()
    _startdt_on(tcp)
    raw = _interrogate(tcp)
    elapsed = (time.monotonic() - t0) * 1000.0
    text = ""
    for i in range(FLAG_LEN):
        w = find_ioa(raw, FLAG_BASE + i)
        if w is None:
            break
        w &= 0xFFFF
        for c in (w >> 8, w & 0xFF):
            if c:
                text += chr(c)
    flag = find_flag(text)
    res = OperationResult("IEC-104", "flag", f"{ctx.target}:{ctx.port('iec104')}",
                          request=f"read flag block IOA {FLAG_BASE}..{FLAG_BASE + FLAG_LEN - 1}",
                          elapsed_ms=elapsed)
    if flag:
        res.verify = Verify.COMPLETED
        res.flag = flag
        res.response = "flag object block populated"
        res.detail = "the RTU exposed the flag -- the challenge completion condition was met"
    else:
        res.verify = Verify.ACKED
        res.response = "flag block reads empty"
        res.detail = "not solved yet (the breaker has not been confirmed open to the challenge logic)"
    return res


def h_wait_until(ctx, ns):
    """Poll a monitored point until it reaches an expected value, or time out. READ-ONLY.

    Reuses the existing STARTDT+interrogation path (no control command is ever sent). Monotonic-clock
    timeout, bounded polling interval, and a small bounded tolerance for transient disconnects (never
    an infinite retry). Observing the value is reported as an OBSERVED state change, never as a solved
    challenge -- confirm completion separately with 'iec104 flag'.
    """
    point, value = ns["point"], ns["value"]
    timeout = ns.get("timeout")
    timeout = 30.0 if timeout is None else timeout     # explicit None check (0 is invalid, not default)
    if timeout <= 0 or timeout > WAIT_MAX_S:
        raise ValueError(f"--timeout must be 1..{WAIT_MAX_S} (seconds)")
    interval = min(max(ns.get("interval") or 1.0, 0.2), 10.0)

    t0 = time.monotonic()
    deadline = t0 + timeout
    polls, observed, matched, errors, err = 0, None, False, 0, ""
    while time.monotonic() < deadline:
        polls += 1
        try:
            tcp = ctx.tcp("iec104")
            try:
                _startdt_on(tcp)
                raw = _interrogate(tcp)
            finally:
                tcp.close()
            observed = find_ioa(raw, point)
            errors = 0
        except TransportError as e:
            errors += 1
            err = str(e)
            if errors > WAIT_MAX_ERRORS:
                break
            if time.monotonic() + interval < deadline:
                time.sleep(interval)
            continue
        if observed == value:
            matched = True
            break
        if time.monotonic() + interval < deadline:
            time.sleep(interval)
        else:
            break

    res = OperationResult("IEC-104", "wait-until", f"{ctx.target}:{ctx.port('iec104')}",
                          request=f"wait IOA {point} == {value} (timeout {timeout}s, interval {interval}s)",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    if matched:
        res.verify = Verify.STATE_CHANGED
        res.response = f"IOA {point} reached {value} after {polls} poll(s)"
        res.detail = ("condition OBSERVED -- not necessarily challenge completion. Run 'iec104 flag' "
                      "to confirm the RTU accepted the change.")
    elif errors > WAIT_MAX_ERRORS:
        res.verify = Verify.ERROR
        res.response = f"gave up after {errors} consecutive transport errors"
        res.detail = f"last error: {err} (bounded -- no infinite retry)"
    else:
        res.verify = Verify.ACKED
        res.response = f"timeout: IOA {point} last read {observed}, never == {value} in {timeout}s"
        res.detail = "the condition was not observed within the timeout; nothing is claimed solved."
    return res


def register(reg):
    reg.add_group("iec104", "IEC-104 (Tripping Breaker, Ch01) -- interrogation & single command")
    reg.add(Command("status", "STARTDT handshake / protocol status", h_status, group="iec104",
                    examples=["iec104 status"]))
    reg.add(Command("interrogate", "General interrogation; list all monitored points", h_interrogate,
                    group="iec104", examples=["iec104 interrogate"]))
    reg.add(Command("read", "Read one IOA's value from an interrogation", h_read, group="iec104",
                    args=[ArgSpec("ioa", "information object address", type="int")],
                    examples=["iec104 read 1001"]))
    reg.add(Command("command", "Send a single command C_SC -- STATE CHANGING", h_command,
                    group="iec104", modifies_state=True,
                    args=[ArgSpec("ioa", "target IOA", type="int"),
                          ArgSpec("value", "SCS value (0 or 1)", type="int"),
                          ArgSpec("--originator", "originator address (from your recon)",
                                  required=True, type="int")],
                    examples=["iec104 command 1001 0 --originator 7"],
                    long_help="Transmits one C_SC. You must supply the IOA, SCS value and originator "
                              "yourself (derive them from the interrogation / evidence). The frame is "
                              "printed before transmission. A positive ACTCON is only an ack."))
    reg.add(Command("flag", "Re-interrogate and read the flag object block (if the RTU exposes it)",
                    h_flag, group="iec104", examples=["iec104 flag"]))
    reg.add(Command("wait-until", "Poll a point until it reaches a value, or time out (read-only)",
                    h_wait_until, group="iec104",
                    args=[ArgSpec("point", "information object address (IOA) to watch", type="int"),
                          ArgSpec("value", "the value to wait for", type="int"),
                          ArgSpec("--timeout", f"overall timeout seconds (1..{WAIT_MAX_S})",
                                  required=False, type="float", default=30.0),
                          ArgSpec("--interval", "seconds between polls (0.2..10)",
                                  required=False, type="float", default=1.0)],
                    examples=["iec104 wait-until 1001 0", "iec104 wait-until 1001 0 --timeout 20 --interval 0.5"],
                    long_help="Reuses interrogation only -- it never sends a control command. Reports an "
                              "OBSERVED state change when the value is seen (distinct from a solved "
                              "challenge); confirm completion with 'iec104 flag'."))
