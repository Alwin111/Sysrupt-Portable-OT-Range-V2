# SPDX-License-Identifier: Apache-2.0
"""Modbus/TCP module -- Maintenance Override (Challenge 12), target port 502.

The MBAP reassembly logic is ported from the repository's reference client ``tools/solve/mbtcp.py``
(a multi-register read can span several TCP segments). Read-only and state-changing operations are
clearly separated; a write acknowledgement is never reported as a solved challenge.

Function codes: FC03 read-holding, FC04 read-input, FC06 write-single, FC16 write-multiple.
"""
from __future__ import annotations

import ipaddress
import struct
import time

from ..registry import ArgSpec, Command, parse_int
from ..result import OperationResult, Verify
from ..timing import Step, run_sequence
from ..util import ascii_from_regs, find_flag, hexdump, parse_hex

WINDOW_MAX_MS = 60000    # bounded race window
HOLD_MAX_ITERS = 200     # bounded hold iterations (never an unbounded loop)

MAX_REGS = 125           # Modbus protocol max per read
MAX_DUMP = 64            # toolkit cap to stay gentle on the badge


class ModbusError(Exception):
    pass


# ---- pure codec (transport-agnostic, unit-testable) ----

def build_read(tid: int, unit: int, fc: int, start: int, count: int) -> bytes:
    pdu = struct.pack(">BHH", fc, start, count)
    return struct.pack(">HHHB", tid, 0, len(pdu) + 1, unit) + pdu


def build_write_single(tid: int, unit: int, addr: int, value: int) -> bytes:
    pdu = struct.pack(">BHH", 6, addr, value & 0xFFFF)
    return struct.pack(">HHHB", tid, 0, len(pdu) + 1, unit) + pdu


def parse_pdu(resp: bytes):
    """Return ('ok', fc, payload) or ('exception', fc, code). resp is the PDU (no MBAP)."""
    if len(resp) < 2:
        raise ModbusError("short response")
    fc = resp[0]
    if fc & 0x80:
        return ("exception", fc & 0x7F, resp[1])
    return ("ok", fc, resp[1:])


def regs_from_read(payload: bytes) -> list:
    bc = payload[0]
    return [struct.unpack(">H", payload[1 + 2 * i:3 + 2 * i])[0] for i in range(bc // 2)]


EXC = {1: "ILLEGAL FUNCTION", 2: "ILLEGAL DATA ADDRESS", 3: "ILLEGAL DATA VALUE",
       4: "SLAVE FAILURE", 6: "SLAVE BUSY"}


class ModbusClient:
    """Thin layer over a connected TcpClient (ctx.tcp('modbus'))."""

    def __init__(self, tcp, unit: int = 1):
        self.tcp = tcp
        self.unit = unit
        self.tid = 0

    def _txn(self, frame: bytes) -> bytes:
        self.tid = (self.tid + 1) & 0xFFFF
        self.tcp.send(frame)
        header = self.tcp.recv_exact(7)          # MBAP header
        if len(header) < 7:
            raise ModbusError("no/short MBAP header (connection closed?)")
        length = struct.unpack(">H", header[4:6])[0]
        body = self.tcp.recv_exact(length - 1)    # length counts unit id + PDU
        return body

    def read(self, fc: int, start: int, count: int):
        pdu = self._txn(build_read(self.tid + 1, self.unit, fc, start, count))
        kind, rfc, payload = parse_pdu(pdu)
        if kind == "exception":
            return ("exception", payload, pdu)
        return ("ok", regs_from_read(payload), pdu)

    def write_single(self, addr: int, value: int):
        pdu = self._txn(build_write_single(self.tid + 1, self.unit, addr, value))
        kind, rfc, payload = parse_pdu(pdu)
        if kind == "exception":
            return ("exception", payload, pdu)
        return ("ok", payload, pdu)


# ---- handlers ----

def _client(ctx) -> ModbusClient:
    return ModbusClient(ctx.tcp("modbus"))


def _read(ctx, ns, fc: int, label: str) -> OperationResult:
    count = ns.get("count") or 1
    if count < 1 or count > MAX_REGS:
        raise ModbusError(f"count must be 1..{MAX_REGS}")
    t0 = time.monotonic()
    kind, data, pdu = _client(ctx).read(fc, ns["addr"], count)
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("Modbus", label, f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"{label} addr={ns['addr']} count={count}", elapsed_ms=elapsed)
    if kind == "exception":
        res.verify = Verify.ACKED
        res.response = f"exception 0x{data:02x} ({EXC.get(data, 'unknown')})"
        res.detail = "the device rejected the request (read-only; nothing changed)"
        return res
    res.verify = Verify.ACKED
    res.response = str(data)
    ascii_view = ascii_from_regs(data)
    res.extra["ascii"] = ascii_view or "(non-printable)"
    flag = find_flag(ascii_view)
    if flag:
        res.verify = Verify.COMPLETED
        res.flag = flag
        res.detail = "flag bytes were present in the device's own register output"
    return res


def h_read_holding(ctx, ns):
    return _read(ctx, ns, 3, "read-holding")


def h_read_input(ctx, ns):
    return _read(ctx, ns, 4, "read-input")


def h_write(ctx, ns):
    addr, value = ns["addr"], ns["value"]
    if not (0 <= value <= 0xFFFF):
        raise ModbusError("value must be 0..65535")
    t0 = time.monotonic()
    kind, data, pdu = _client(ctx).write_single(addr, value)
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("Modbus", "write-holding", f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"write HR{addr} := {value}", elapsed_ms=elapsed)
    if kind == "exception":
        res.verify = Verify.SENT
        res.response = f"exception 0x{data:02x} ({EXC.get(data, 'unknown')})"
        res.detail = "write REJECTED by the device (e.g. address not writable in current state)"
        return res
    res.verify = Verify.ACKED
    res.response = "echo (write acknowledged)"
    res.detail = ("acknowledgement only -- NOT proof the challenge accepted it. Read the register "
                  "back (or the relevant status point) to confirm an actual state change.")
    return res


def h_verify(ctx, ns):
    """Read a register back and compare to an expected value (observes state; not completion)."""
    fc = 4 if ns.get("input") else 3
    t0 = time.monotonic()
    kind, data, _ = _client(ctx).read(fc, ns["addr"], 1)
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("Modbus", "verify", f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"read {'IR' if fc == 4 else 'HR'}{ns['addr']} == {ns['expected']}?",
                          elapsed_ms=elapsed)
    if kind == "exception":
        res.verify = Verify.ACKED
        res.response = f"exception 0x{data:02x}"
        return res
    got = data[0] if data else None
    res.response = f"read {got}"
    if got == ns["expected"]:
        res.verify = Verify.STATE_CHANGED
        res.detail = "observed value matches expected (state confirmed; not necessarily completion)"
    else:
        res.verify = Verify.ACKED
        res.detail = "value does not match expected"
    return res


def h_dump(ctx, ns):
    start, count = ns["start"], ns["count"]
    if count < 1 or count > MAX_DUMP:
        raise ModbusError(f"count must be 1..{MAX_DUMP}")
    fc = 4 if ns.get("input") else 3
    t0 = time.monotonic()
    kind, data, _ = _client(ctx).read(fc, start, count)
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("Modbus", "dump", f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"dump {'IR' if fc == 4 else 'HR'}{start}..{start + count - 1}",
                          elapsed_ms=elapsed)
    if kind == "exception":
        res.verify = Verify.ACKED
        res.response = f"exception 0x{data:02x}"
        return res
    res.verify = Verify.ACKED
    rows = [f"  {'IR' if fc == 4 else 'HR'}{start + i}: {v}  (0x{v:04x})" for i, v in enumerate(data)]
    res.response = f"{len(data)} registers"
    res.extra["dump"] = "\n" + "\n".join(rows)
    ascii_view = ascii_from_regs(data)
    if ascii_view:
        res.extra["ascii"] = ascii_view
    flag = find_flag(ascii_view)
    if flag:
        res.verify = Verify.COMPLETED
        res.flag = flag
    return res


def h_monitor(ctx, ns):
    """Poll one register a bounded number of times (gentle; no aggressive looping)."""
    fc = 4 if ns.get("input") else 3
    samples = min(max(ns.get("samples") or 5, 1), 30)
    interval = min(max(ns.get("interval") or 1.0, 0.2), 10.0)
    client = _client(ctx)
    seen = []
    t0 = time.monotonic()
    for i in range(samples):
        kind, data, _ = client.read(fc, ns["addr"], 1)
        seen.append(data[0] if kind == "ok" and data else None)
        if i < samples - 1:
            time.sleep(interval)
    elapsed = (time.monotonic() - t0) * 1000.0
    res = OperationResult("Modbus", "monitor", f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"monitor {'IR' if fc == 4 else 'HR'}{ns['addr']} x{samples}@{interval}s",
                          elapsed_ms=elapsed)
    res.verify = Verify.ACKED
    res.response = " ".join(str(v) for v in seen)
    res.detail = "changed" if len(set(seen)) > 1 else "stable"
    return res


def h_decode(ctx, ns):
    data = parse_hex(ns["hex"])
    regs = [struct.unpack(">H", data[i:i + 2])[0] for i in range(0, len(data) - 1, 2)]
    res = OperationResult("Modbus", "decode", "(offline)", request=hexdump(data))
    res.verify = Verify.ACKED
    lines = [f"  uint16: {regs}",
             f"  int16:  {[struct.unpack('>h', struct.pack('>H', r))[0] for r in regs]}",
             f"  ascii:  {ascii_from_regs(regs) or '(non-printable)'}"]
    res.response = "decoded"
    res.extra["decode"] = "\n" + "\n".join(lines)
    return res


# ---- Feature: RACE (timing-sensitive unlock->write) + HOLD (bounded maintain) ----

def _parse_pair(text: str):
    """Parse a STRICT '<reg>=<value>' pair into (reg, val) ints. Never guesses; raises on malformed."""
    if text is None or "=" not in text:
        raise ModbusError(f"expected <reg>=<value>, got {text!r}")
    left, _, right = text.partition("=")
    left, right = left.strip(), right.strip()
    if not left or not right:
        raise ModbusError(f"expected <reg>=<value>, got {text!r}")
    try:
        reg, val = parse_int(left), parse_int(right)
    except Exception as e:
        raise ModbusError(f"invalid register/value in {text!r}: {e}") from e
    if not (0 <= reg <= 0xFFFF):
        raise ModbusError(f"register out of range in {text!r} (0..65535)")
    if not (0 <= val <= 0xFFFF):
        raise ModbusError(f"value out of range in {text!r} (0..65535)")
    return reg, val


def _is_lab_target(host: str) -> bool:
    """True only for a loopback/private (lab/simulator) address. 'hold' refuses to write otherwise."""
    try:
        ip = ipaddress.ip_address(host)
    except ValueError:
        return False     # an unclassifiable hostname is NOT treated as a known lab target
    return ip.is_loopback or ip.is_private


def h_race(ctx, ns):
    """Two writes (unlock -> target) back-to-back over ONE persistent connection, timing the gap.

    This is sugar over the EXISTING sequence engine (run_sequence): one connection, each step run
    exactly once, no auto-retry of a state-changing write -- it just saves hand-writing a sequence
    JSON file. Two acknowledgements and a short gap are NOT success; the target is read back to
    observe the outcome, and completion is still only ever confirmed by the device (flag/status).
    """
    unlock_reg, unlock_val = _parse_pair(ns["unlock"])
    target_reg, target_val = _parse_pair(ns["target"])
    window = ns.get("window")
    if window is None or window <= 0 or window > WINDOW_MAX_MS:
        raise ModbusError(f"--window must be a positive integer 1..{WINDOW_MAX_MS} (ms)")

    steps = [
        Step(command=f"modbus write {unlock_reg} {unlock_val}", name="unlock"),
        Step(command=f"modbus write {target_reg} {target_val}", name="target", deadline_ms=window),
    ]

    def ex(tokens):
        # Only 'modbus write <addr> <value>' steps -- reuse the real write handler/codec, no duplication.
        if tokens[:2] != ["modbus", "write"] or len(tokens) != 4:
            raise ModbusError(f"race executor only runs 'modbus write' steps, got {tokens}")
        return h_write(ctx, {"addr": parse_int(tokens[2]), "value": parse_int(tokens[3])})

    prev_keepalive = ctx.keepalive
    ctx.keepalive = True                        # both writes share one connection (the race window)
    try:
        report = run_sequence("modbus-race", steps, executor=ex, sleep=time.sleep)
        try:
            vkind, vdata, _ = _client(ctx).read(3, target_reg, 1)   # observe the outcome
        except Exception:
            vkind, vdata = "error", None                            # a failed read -> no observation
    finally:
        ctx.keepalive = prev_keepalive
        if not prev_keepalive:
            ctx.close_all()                     # close the persistent connection we opened

    gap_ms = report.total_ms
    window_met = gap_ms <= window
    res = OperationResult("Modbus", "race", f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"unlock HR{unlock_reg}:={unlock_val} -> target HR{target_reg}:={target_val} "
                                  f"within {window} ms", elapsed_ms=gap_ms)
    lines, acked = [], 0
    for sr in report.steps:
        r = sr.result
        lvl = r.verify.label if r else "(no result)"
        note = (r.detail if r else sr.error) or ""
        if r and r.verify >= Verify.ACKED:
            acked += 1
        lines.append(f"  {sr.step.name:<7} {lvl}  ({sr.elapsed_ms:.1f} ms)" + (f"  [{note}]" if note else ""))
    res.extra["operations"] = "\n" + "\n".join(lines)
    observed = vdata[0] if vkind == "ok" and vdata else None
    res.extra["observed"] = (f"HR{target_reg} = {observed} (expected {target_val})"
                             if observed is not None else f"HR{target_reg}: no reading")
    timing = f"writes landed in {gap_ms:.1f} ms (window {window} ms: {'met' if window_met else 'MISSED'})"
    if observed == target_val:
        res.verify = Verify.STATE_CHANGED
        res.response = f"target value present (HR{target_reg}={observed})"
        res.detail = (f"{timing}; value observed. This is an OBSERVED state change, NOT challenge "
                      f"completion -- read the flag/status to confirm the challenge accepted it.")
    else:
        res.verify = Verify.ACKED if acked else Verify.SENT
        res.response = f"target value NOT observed (HR{target_reg}={observed})"
        res.detail = (f"{timing}; the written value did not stick (a reconciliation loop may have "
                      f"reverted it, or the window was missed). Not reported as solved.")
    return res


def h_hold(ctx, ns):
    """Bounded 'apply an adjustment and hold until a condition is observed'. SAFE FRAMEWORK ONLY.

    It applies ONLY the explicit <reg>=<val> adjustment you give, reads current state each iteration,
    and stops as soon as your explicit --until <reg>=<val> condition is observed (or on a bound /
    error). It never invents a register, value, formula or control algorithm. Writes are allowed only
    against a loopback/private (lab/simulator) target.
    """
    adj_reg, adj_val = _parse_pair(ns["adjust"])
    cond_reg, cond_val = _parse_pair(ns["until"])
    cond_fc = 4 if ns.get("until_input") else 3
    timeout = ns.get("timeout")
    timeout = 10.0 if timeout is None else timeout     # explicit None check (0 is invalid, not default)
    if timeout <= 0 or timeout > 300:
        raise ModbusError("--timeout must be 1..300 (seconds)")
    interval = min(max(ns.get("interval") or 1.0, 0.2), 10.0)
    max_iters = min(max(int(ns.get("max_iters") or HOLD_MAX_ITERS), 1), HOLD_MAX_ITERS)
    if not _is_lab_target(ctx.target):
        raise ModbusError(f"'modbus hold' writes are restricted to a loopback/private lab target; "
                          f"{ctx.target!r} is not one. Point the toolkit at the CTF simulator "
                          f"(default 10.0.1.1) before using hold.")

    cond_label = f"{'IR' if cond_fc == 4 else 'HR'}{cond_reg}"
    client = _client(ctx)
    res = OperationResult("Modbus", "hold", f"{ctx.target}:{ctx.port('modbus')}",
                          request=f"hold HR{adj_reg}:={adj_val} until {cond_label}=={cond_val} "
                                  f"(timeout {timeout}s, interval {interval}s, max {max_iters} iters)")
    rows, iters, satisfied, last_obs, err = [], 0, False, None, ""
    t0 = time.monotonic()
    deadline = t0 + timeout
    try:
        while iters < max_iters and time.monotonic() < deadline:
            iters += 1
            kind, data, _ = client.read(cond_fc, cond_reg, 1)          # read state BEFORE deciding
            last_obs = data[0] if kind == "ok" and data else None
            if last_obs == cond_val:
                satisfied = True
                rows.append(f"  iter {iters}: condition observed ({cond_label}={last_obs})")
                break
            wkind, _, _ = client.write_single(adj_reg, adj_val)        # apply ONLY the configured write
            rows.append(f"  iter {iters}: {cond_label}={last_obs} != {cond_val}; wrote HR{adj_reg}:={adj_val} "
                        f"({'acked' if wkind == 'ok' else 'rejected'})")
            if time.monotonic() + interval < deadline:
                time.sleep(interval)
            else:
                break
    except Exception as e:                                             # transport/protocol error -> stop
        err = str(e)
    res.extra["iterations"] = "\n" + "\n".join(rows) if rows else "(none)"
    res.elapsed_ms = (time.monotonic() - t0) * 1000.0
    if err:
        res.verify = Verify.ERROR
        res.response = f"stopped on error after {iters} iter(s)"
        res.detail = f"aborted: {err} (no infinite retry)"
    elif satisfied:
        res.verify = Verify.STATE_CHANGED
        res.response = f"condition observed after {iters} iter(s)"
        res.detail = ("the configured condition was observed -- an OBSERVED state change, NOT "
                      "necessarily challenge completion. Read the flag/status to confirm.")
    else:
        res.verify = Verify.ACKED
        res.response = f"condition NOT observed within {iters} iter(s) / {timeout}s"
        res.detail = "the condition was never observed; nothing is claimed solved."
    return res


def register(reg):
    reg.add_group("modbus", "Modbus/TCP (Reactor challenges, Ch07/Ch08) -- registers & writes")
    reg.add(Command("read-holding", "Read holding registers (FC03)", h_read_holding, group="modbus",
                    args=[ArgSpec("addr", "start address", type="int"),
                          ArgSpec("count", "number of registers", required=False, type="int", default=1)],
                    examples=["modbus read-holding 20 2"]))
    reg.add(Command("read-input", "Read input registers (FC04, read-only sensors)", h_read_input,
                    group="modbus",
                    args=[ArgSpec("addr", "start address", type="int"),
                          ArgSpec("count", "number of registers", required=False, type="int", default=1)],
                    examples=["modbus read-input 70 5"]))
    reg.add(Command("write", "Write a single holding register (FC06) -- STATE CHANGING",
                    h_write, group="modbus", modifies_state=True,
                    args=[ArgSpec("addr", "address", type="int"),
                          ArgSpec("value", "value 0..65535", type="int")],
                    examples=["modbus write 20 1", "modbus write 21 100"],
                    long_help="Writes one holding register. The acknowledgement is only an ack -- "
                              "confirm the real effect with 'modbus verify' or by reading a status point."))
    reg.add(Command("verify", "Read a register back and compare to an expected value", h_verify,
                    group="modbus",
                    args=[ArgSpec("addr", "address", type="int"),
                          ArgSpec("expected", "expected value", type="int"),
                          ArgSpec("--input", "read an input register instead of holding",
                                  required=False, is_flag=True, default=False)],
                    examples=["modbus verify 73 100 --input"]))
    reg.add(Command("dump", "Read and pretty-print a range of registers", h_dump, group="modbus",
                    args=[ArgSpec("start", "start address", type="int"),
                          ArgSpec("count", f"count (1..{MAX_DUMP})", type="int"),
                          ArgSpec("--input", "input registers instead of holding",
                                  required=False, is_flag=True, default=False)],
                    examples=["modbus dump 0 8", "modbus dump 48 16 --input"]))
    reg.add(Command("monitor", "Poll one register a bounded number of times", h_monitor, group="modbus",
                    args=[ArgSpec("addr", "address", type="int"),
                          ArgSpec("--input", "input register", required=False, is_flag=True, default=False),
                          ArgSpec("--samples", "number of samples (1..30)", required=False, type="int", default=5),
                          ArgSpec("--interval", "seconds between samples", required=False, type="float", default=1.0)],
                    examples=["modbus monitor 72 --input --samples 10 --interval 0.5"]))
    reg.add(Command("decode", "Decode a register hex blob to uint16/int16/ascii (offline)", h_decode,
                    group="modbus",
                    args=[ArgSpec("hex", "hex bytes, e.g. '01 90 00 64'")],
                    examples=["modbus decode '01 90 00 64'"]))
    reg.add(Command("race", "Time-boxed unlock->write over one connection -- STATE CHANGING", h_race,
                    group="modbus", modifies_state=True,
                    args=[ArgSpec("unlock", "the unlock write as <reg>=<value>"),
                          ArgSpec("target", "the target write as <reg>=<value>"),
                          ArgSpec("--window", f"max gap in ms (1..{WINDOW_MAX_MS})", required=True, type="int")],
                    examples=["modbus race 20=1 21=100 --window 500"],
                    long_help="Runs exactly two writes (unlock then target) in order over ONE persistent "
                              "connection via the existing sequence engine -- no second timing engine, and "
                              "the state-changing writes are never auto-retried. You supply both registers, "
                              "both values and the window; nothing is inferred. The target is read back to "
                              "observe the outcome -- an observed value is NOT challenge completion."))
    reg.add(Command("hold", "Apply an adjustment and hold until a condition -- STATE CHANGING", h_hold,
                    group="modbus", modifies_state=True,
                    args=[ArgSpec("adjust", "the adjustment to apply each iteration, as <reg>=<value>"),
                          ArgSpec("--until", "stop when this condition holds, as <reg>=<value>", required=True),
                          ArgSpec("--until-input", "read the condition register as an input register (FC04)",
                                  required=False, is_flag=True, default=False),
                          ArgSpec("--timeout", "overall timeout seconds (1..300)", required=False, type="float", default=10.0),
                          ArgSpec("--interval", "seconds between iterations (0.2..10)", required=False, type="float", default=1.0),
                          ArgSpec("--max-iters", f"max iterations (1..{HOLD_MAX_ITERS})", required=False, type="int",
                                  default=HOLD_MAX_ITERS, metavar="N")],
                    examples=["modbus hold 73=100 --until 74=1 --timeout 12 --interval 0.5"],
                    long_help="A SAFE FRAMEWORK, not a controller: it writes ONLY the <reg>=<value> you give, "
                              "reads state before each write, and stops as soon as your explicit --until "
                              "condition is observed (or on timeout / max-iters / error). It never invents a "
                              "register, value or control formula. Writes are allowed only against a "
                              "loopback/private lab target. An observed condition is NOT challenge completion."))
