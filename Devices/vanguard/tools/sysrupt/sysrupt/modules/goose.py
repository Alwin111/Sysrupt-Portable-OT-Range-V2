# SPDX-License-Identifier: Apache-2.0
"""GOOSE module -- Second Blackout (Challenge 06), target UDP port 6180.

The simulator publishes a 16-byte application-level training frame over UDP (not raw-ethernet IEC
61850 GOOSE), so passive inspection needs no special privileges -- it is an ordinary UDP receive.
Frame format (big-endian), ported from ``tools/solve/ch06_goose.py``:

    'G''S' (47 53) | ver(1) | flags(1) | APPID(2) | stNum(4) | sqNum(4) | confRev(2)
    flags bit0 = breaker-open, bit1 = test

Mode A (sniff/decode) is strictly passive -- it never transmits. Mode B (send) constructs a frame
from values the player supplies (nothing is inferred or defaulted to the "correct" value), prints
the hex, and transmits only on the explicit `send` command.
"""
from __future__ import annotations

import struct
import time

from ..registry import ArgSpec, Command
from ..result import OperationResult, Verify
from ..transport import TransportError
from ..util import find_flag, hexdump, parse_hex

MARKER = b"GS"
FRAME_LEN = 16
SUSTAIN_MAX_S = 30        # conservative duration cap
SUSTAIN_MIN_INTERVAL = 0.2  # send-rate cap (<= 5 frames/s)
SUSTAIN_MAX_FRAMES = 300  # absolute frame cap regardless of duration/interval


def decode_frame(data: bytes) -> dict:
    if len(data) < FRAME_LEN or data[:2] != MARKER:
        raise ValueError("not a GOOSE training frame (expected 16 bytes starting 'GS')")
    ver = data[2]
    flags = data[3]
    appid = struct.unpack(">H", data[4:6])[0]
    stnum = struct.unpack(">I", data[6:10])[0]
    sqnum = struct.unpack(">I", data[10:14])[0]
    confrev = struct.unpack(">H", data[14:16])[0]
    return {"ver": ver, "flags": flags, "appid": appid, "stNum": stnum, "sqNum": sqnum,
            "confRev": confrev, "breaker_open": bool(flags & 1), "test": bool(flags & 2)}


def build_frame(appid: int, stnum: int, sqnum: int, confrev: int, flags: int) -> bytes:
    return (MARKER + bytes([1, flags & 0xFF]) + struct.pack(">H", appid & 0xFFFF) +
            struct.pack(">I", stnum & 0xFFFFFFFF) + struct.pack(">I", sqnum & 0xFFFFFFFF) +
            struct.pack(">H", confrev & 0xFFFF))


def _fmt_frame(d: dict) -> str:
    return (f"APPID=0x{d['appid']:04x} stNum={d['stNum']} sqNum={d['sqNum']} "
            f"confRev={d['confRev']} flags=0x{d['flags']:02x} "
            f"(breaker_open={d['breaker_open']}, test={d['test']})")


# ---- handlers ----

def h_sniff(ctx, ns):
    """Passive capture for a bounded duration. Transmits nothing."""
    seconds = min(max(ns.get("seconds") or 4, 1), 30)
    udp = ctx.udp("goose").open_rx()
    frames = []
    t0 = time.monotonic()
    deadline = t0 + seconds
    try:
        while time.monotonic() < deadline:
            remaining = max(0.2, deadline - time.monotonic())
            udp.sock.settimeout(remaining)
            got = udp.recv(64)
            if got is None:
                continue
            data, _addr = got
            try:
                frames.append((data, decode_frame(data)))
            except ValueError:
                frames.append((data, None))      # record malformed frames too
    except KeyboardInterrupt:
        pass
    finally:
        udp.close()
    res = OperationResult("GOOSE", "sniff (passive)", f"{ctx.target}:{ctx.port('goose')}",
                          request=f"listen {seconds}s on UDP {ctx.port('goose')}",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    res.verify = Verify.ACKED if frames else Verify.SENT
    res.response = f"{len(frames)} frame(s) captured"
    rows = []
    for data, d in frames:
        rows.append(f"  {hexdump(data)}  ::  " + (_fmt_frame(d) if d else "(malformed/unsupported)"))
    res.extra["frames"] = "\n" + "\n".join(rows) if rows else "(none -- publisher idle or wrong port)"
    if ns.get("save") and frames:
        _save_capture(ns["save"], frames)
        res.detail = f"saved {len(frames)} frame(s) to {ns['save']}"
    return res


def _save_capture(path: str, frames) -> None:
    import json
    with open(path, "w", encoding="utf-8") as fh:
        json.dump([data.hex() for data, _ in frames], fh)


def h_capture_show(ctx, ns):
    import json
    with open(ns["file"], "r", encoding="utf-8") as fh:
        hexes = json.load(fh)
    res = OperationResult("GOOSE", "capture show", "(offline)", request=ns["file"])
    res.verify = Verify.ACKED
    rows = []
    for h in hexes:
        data = bytes.fromhex(h)
        try:
            rows.append(f"  {hexdump(data)}  ::  {_fmt_frame(decode_frame(data))}")
        except ValueError:
            rows.append(f"  {hexdump(data)}  ::  (malformed)")
    res.response = f"{len(hexes)} frame(s)"
    res.extra["frames"] = "\n" + "\n".join(rows)
    return res


def h_decode(ctx, ns):
    data = parse_hex(ns["hex"])
    res = OperationResult("GOOSE", "decode", "(offline)", request=hexdump(data))
    try:
        d = decode_frame(data)
        res.verify = Verify.ACKED
        res.response = _fmt_frame(d)
    except ValueError as e:
        res.verify = Verify.SENT
        res.response = str(e)
    return res


def h_send(ctx, ns):
    """Construct and transmit a frame from EXPLICIT player-supplied fields. STATE CHANGING."""
    flags = ns.get("flags")
    if flags is None:
        flags = (0x01 if ns.get("open") else 0x00) | (0x02 if ns.get("test") else 0x00)
    frame = build_frame(ns["appid"], ns["stnum"], ns["sqnum"], ns["confrev"], flags)
    decoded = decode_frame(frame)
    udp = ctx.udp("goose").open_tx()
    t0 = time.monotonic()
    try:
        udp.sendto(frame)
        got = udp.recv(128)
    finally:
        udp.close()
    res = OperationResult("GOOSE", "send", f"{ctx.target}:{ctx.port('goose')}",
                          request=f"{hexdump(frame)}  ::  {_fmt_frame(decoded)}",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    if got is None:
        res.verify = Verify.SENT
        res.response = "(no reply)"
        res.detail = "transmitted; no reply (if the publisher's stNum moved, re-sniff and use a higher one)"
        return res
    reply = got[0].decode("latin1", "replace").strip()
    res.response = reply
    flag = find_flag(reply)
    if flag:
        res.verify = Verify.COMPLETED
        res.flag = flag
        res.detail = "the simulator accepted the frame and returned the flag -- completion confirmed"
    elif "ACCEPTED" in reply.upper():
        res.verify = Verify.ACKED
        res.detail = "frame acknowledged; no flag in reply"
    else:
        res.verify = Verify.SENT
        res.detail = "frame sent; reply did not indicate acceptance"
    return res


def h_sustain(ctx, ns):
    """Hold a GOOSE state by re-sending the SAME stNum with an incrementing sqNum for a bounded time.

    This reuses the existing frame encoder and sends unicast to the CONFIGURED target only (no
    broadcast discovery, no interface selection, no scanning). sqNum progresses 0,1,2,... while stNum
    and confRev stay fixed -- exactly the retransmission semantics of the reference solver. Duration
    and send-rate are capped. Transmitting frames is NEVER reported as success: completion is claimed
    only if the simulator's own reply carries the flag. Use --dry-run to preview without transmitting.
    """
    appid, stnum, confrev = ns["appid"], ns["stnum"], ns["confrev"]
    duration = ns["duration"]
    for nm, v, hi in (("appid", appid, 0xFFFF), ("stnum", stnum, 0xFFFFFFFF), ("confrev", confrev, 0xFFFF)):
        if v is None or v < 0 or v > hi:
            raise ValueError(f"--{nm} out of range (0..{hi})")
    if duration is None or duration <= 0 or duration > SUSTAIN_MAX_S:
        raise ValueError(f"--duration must be 1..{SUSTAIN_MAX_S} (seconds)")
    interval = min(max(ns.get("interval") or 1.0, SUSTAIN_MIN_INTERVAL), 5.0)
    flags = ns.get("flags")
    if flags is None:
        flags = (0x01 if ns.get("open") else 0x00) | (0x02 if ns.get("test") else 0x00)
    flags &= 0xFF
    max_frames = min(int(duration / interval) + 1, SUSTAIN_MAX_FRAMES)

    base = (f"stNum={stnum} appid=0x{appid:04x} confRev={confrev} flags=0x{flags:02x} "
            f"(breaker_open={bool(flags & 1)}, test={bool(flags & 2)})")

    if ns.get("dry_run"):
        preview = [f"  sqNum={sq}: {hexdump(build_frame(appid, stnum, sq, confrev, flags))}"
                   for sq in range(min(3, max_frames))]
        res = OperationResult("GOOSE", "sustain (dry-run)", f"{ctx.target}:{ctx.port('goose')}",
                              request=f"PREVIEW {base}, sqNum 0..{max_frames - 1} over {duration}s @ {interval}s")
        res.verify = Verify.SENT
        res.response = f"would send ~{max_frames} frame(s); nothing transmitted"
        res.extra["preview"] = "\n" + "\n".join(preview)
        res.detail = "dry-run only -- no frames were sent."
        return res

    udp = ctx.udp("goose").open_tx()
    sent, flag, last_reply, err = 0, None, "", ""
    t0 = time.monotonic()
    deadline = t0 + duration
    sq = 0
    try:
        while time.monotonic() < deadline and sent < max_frames:
            udp.sendto(build_frame(appid, stnum, sq, confrev, flags))
            sent += 1
            got = udp.recv(128)
            if got is not None:
                last_reply = got[0].decode("latin1", "replace").strip()
                f = find_flag(last_reply)
                if f:
                    flag = f
                    break
            sq += 1
            if time.monotonic() + interval < deadline and sent < max_frames:
                time.sleep(interval)
            else:
                break
    except KeyboardInterrupt:
        err = "interrupted"
    except TransportError as e:
        err = str(e)
    finally:
        udp.close()

    res = OperationResult("GOOSE", "sustain", f"{ctx.target}:{ctx.port('goose')}",
                          request=f"{base}, sqNum 0..{max(sent - 1, 0)} over {duration}s @ {interval}s",
                          elapsed_ms=(time.monotonic() - t0) * 1000.0)
    if flag:
        res.verify = Verify.COMPLETED
        res.flag = flag
        res.response = f"{sent} frame(s) sent; simulator returned the flag"
        res.detail = "completion confirmed by the device's own reply."
    elif err == "interrupted":
        res.verify = Verify.SENT
        res.response = f"interrupted after {sent} frame(s)"
        res.detail = "stopped cleanly; transmitting frames is not success."
    elif err:
        res.verify = Verify.ERROR
        res.response = f"transport error after {sent} frame(s)"
        res.detail = f"{err} (stopped; no infinite retry)"
    elif last_reply and "ACCEPTED" in last_reply.upper():
        res.verify = Verify.ACKED
        res.response = f"{sent} frame(s) sent; last reply: {last_reply}"
        res.detail = "frames acknowledged; no flag in the reply -- not reported as solved."
    else:
        res.verify = Verify.SENT
        res.response = f"{sent} frame(s) sent; no completion signal"
        res.detail = "transmitted; no reply indicated acceptance. Sending frames is NOT success."
    return res


def register(reg):
    reg.add_group("goose", "GOOSE (Second Blackout, Ch06) -- passive sniff & explicit transmit")
    reg.add(Command("sniff", "Passively capture GOOSE frames for N seconds (sends nothing)", h_sniff,
                    group="goose",
                    args=[ArgSpec("--seconds", "capture duration (1..30)", required=False, type="int", default=4),
                          ArgSpec("--save", "save captured frames to a JSON file", required=False, default="")],
                    examples=["goose sniff --seconds 5", "goose sniff --seconds 5 --save cap.json"]))
    reg.add(Command("decode", "Decode a single GOOSE frame from hex (offline)", h_decode, group="goose",
                    args=[ArgSpec("hex", "16-byte frame hex")],
                    examples=["goose decode '47 53 01 01 0b b8 00 00 00 05 00 00 00 00 00 01'"]))
    reg.add(Command("capture", "Show a previously saved capture file", h_capture_show, group="goose",
                    args=[ArgSpec("show", "literal 'show'", choices=["show"]),
                          ArgSpec("file", "capture file path")],
                    examples=["goose capture show cap.json"]))
    reg.add(Command("send", "Construct and transmit a GOOSE frame -- STATE CHANGING", h_send,
                    group="goose", modifies_state=True,
                    args=[ArgSpec("--appid", "APPID (from your sniff)", required=True, type="int"),
                          ArgSpec("--stnum", "stNum (you choose, e.g. last+1)", required=True, type="int"),
                          ArgSpec("--confrev", "confRev (from your sniff)", required=True, type="int"),
                          ArgSpec("--sqnum", "sqNum", required=False, type="int", default=0),
                          ArgSpec("--open", "set breaker-open flag (bit0)", required=False, is_flag=True, default=False),
                          ArgSpec("--test", "set test flag (bit1)", required=False, is_flag=True, default=False),
                          ArgSpec("--flags", "raw flags byte (overrides --open/--test)", required=False, type="int", default=None)],
                    examples=["goose send --appid 0x0bb8 --stnum 6 --confrev 1 --open"],
                    long_help="Builds the frame from the values you supply (nothing is inferred). The "
                              "frame hex is printed before transmission. Sniff first to learn APPID, "
                              "confRev and the current stNum."))
    reg.add(Command("sustain", "Hold a state: re-send one stNum (sqNum++) for a bounded time -- STATE CHANGING",
                    h_sustain, group="goose", modifies_state=True,
                    args=[ArgSpec("--appid", "APPID (from your sniff)", required=True, type="int"),
                          ArgSpec("--stnum", "stNum to hold (the state change you are sustaining)", required=True, type="int"),
                          ArgSpec("--confrev", "confRev (from your sniff)", required=True, type="int"),
                          ArgSpec("--duration", f"seconds to sustain (1..{SUSTAIN_MAX_S})", required=True, type="int"),
                          ArgSpec("--interval", "seconds between frames (0.2..5)", required=False, type="float", default=1.0),
                          ArgSpec("--open", "set breaker-open flag (bit0)", required=False, is_flag=True, default=False),
                          ArgSpec("--test", "set test flag (bit1)", required=False, is_flag=True, default=False),
                          ArgSpec("--flags", "raw flags byte (overrides --open/--test)", required=False, type="int", default=None),
                          ArgSpec("--dry-run", "preview the frames without transmitting", required=False, is_flag=True, default=False)],
                    examples=["goose sustain --appid 0x0bb8 --stnum 6 --confrev 1 --open --duration 5"],
                    long_help="Reuses the send encoder; unicast to the CONFIGURED target only (no broadcast, "
                              "no interface selection, no scanning). sqNum progresses 0,1,2,... while stNum/"
                              "confRev stay fixed. Duration and send-rate are capped. Transmitting is never "
                              "success -- completion is claimed only if the device's reply carries the flag. "
                              "Use --dry-run to preview first."))
