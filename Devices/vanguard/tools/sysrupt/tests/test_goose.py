# SPDX-License-Identifier: Apache-2.0
import pytest

from sysrupt.modules import goose as G
from sysrupt.result import Verify
from _fakes import FakeCtx, FakeUdp


def test_build_decode_roundtrip():
    frame = G.build_frame(appid=0x0bb8, stnum=6, sqnum=0, confrev=1, flags=0x01)
    d = G.decode_frame(frame)
    assert d["appid"] == 0x0bb8 and d["stNum"] == 6 and d["breaker_open"] and not d["test"]


def test_decode_rejects_non_goose():
    with pytest.raises(ValueError):
        G.decode_frame(b"\x00" * 16)


def test_decode_handler_offline():
    res = G.h_decode(None, {"hex": "47 53 01 03 0b b8 00 00 00 05 00 00 00 00 00 01"})
    assert "test=True" in res.response and "breaker_open=True" in res.response


def test_sniff_is_passive_and_captures(monkeypatch):
    frames = [G.build_frame(0x0bb8, 5, 0, 1, 0x00), b"\xff\xff"]  # one valid, one malformed
    udp = FakeUdp(rx_frames=frames, raise_after=True)             # ends the loop immediately
    ctx = FakeCtx(udp=udp)
    res = G.h_sniff(ctx, {"seconds": 5})
    assert udp.sent == []                      # PASSIVE: transmitted nothing
    assert "1 frame" not in res.response       # captured 2 (valid + malformed)
    assert "2 frame" in res.response
    assert "malformed" in res.extra["frames"]


def test_send_requires_transmit_and_reports_completion():
    reply = b"ACCEPTED breaker=OPEN flag{goose_demo}"
    udp = FakeUdp(reply=reply)
    ctx = FakeCtx(udp=udp)
    res = G.h_send(ctx, {"appid": 0x0bb8, "stnum": 6, "confrev": 1, "sqnum": 0,
                         "open": True, "test": False, "flags": None})
    assert len(udp.sent) == 1                  # exactly one explicit transmission
    assert res.verify == Verify.COMPLETED and res.flag == "flag{goose_demo}"


def test_send_ack_without_flag_is_not_completion():
    udp = FakeUdp(reply=b"ACCEPTED breaker=OPEN")
    ctx = FakeCtx(udp=udp)
    res = G.h_send(ctx, {"appid": 0x0bb8, "stnum": 6, "confrev": 1, "sqnum": 0,
                         "open": True, "test": False, "flags": None})
    assert res.verify == Verify.ACKED and not res.success


def test_send_no_reply_is_sent_only():
    udp = FakeUdp(reply=None)
    ctx = FakeCtx(udp=udp)
    res = G.h_send(ctx, {"appid": 0x0bb8, "stnum": 6, "confrev": 1, "sqnum": 0,
                         "open": False, "test": False, "flags": 0x00})
    assert res.verify == Verify.SENT and len(udp.sent) == 1


def test_flags_override_open_test():
    udp = FakeUdp(reply=None)
    ctx = FakeCtx(udp=udp)
    G.h_send(ctx, {"appid": 1, "stnum": 1, "confrev": 1, "sqnum": 0,
                   "open": True, "test": True, "flags": 0x00})   # explicit flags win
    assert udp.sent[0][3] == 0x00
