# SPDX-License-Identifier: Apache-2.0
"""Tests for `modbus race` (Feature 1): strict pair parsing, window validation, ordering, reporting,
transport failure, and unsuccessful verification. All transports are faked; no sockets, no badge."""
import struct

import pytest

from sysrupt.modules import modbus as M
from sysrupt.result import Verify
from sysrupt.transport import TransportError
from _fakes import FakeCtx, mb_read_resp, mb_write_echo


def _addr_of(frame: bytes) -> int:
    # MBAP(7) + FC(1) + addr(2) ... for an FC06 write-single request.
    return struct.unpack(">H", frame[8:10])[0]


# ---- argument parsing ----

def test_parse_pair_valid_and_hex():
    assert M._parse_pair("20=1") == (20, 1)
    assert M._parse_pair(" 0x14 = 0x64 ") == (20, 100)


@pytest.mark.parametrize("bad", ["20", "=1", "20=", "x=1", "20=y", "", "20=1=2"])
def test_parse_pair_rejects_malformed(bad):
    with pytest.raises(M.ModbusError):
        M._parse_pair(bad)


def test_parse_pair_rejects_out_of_range():
    with pytest.raises(M.ModbusError):
        M._parse_pair("70000=1")
    with pytest.raises(M.ModbusError):
        M._parse_pair("1=70000")


@pytest.mark.parametrize("win", [0, -5, None, 60001])
def test_invalid_window_rejected(win):
    ctx = FakeCtx(tcp_responses=[mb_write_echo(20, 1), mb_write_echo(21, 100), mb_read_resp([100])])
    with pytest.raises(M.ModbusError):
        M.h_race(ctx, {"unlock": "20=1", "target": "21=100", "window": win})


# ---- happy path: ordering + observed outcome ----

def test_race_runs_two_writes_in_order_and_observes_target():
    ctx = FakeCtx(tcp_responses=[mb_write_echo(20, 1), mb_write_echo(21, 100), mb_read_resp([100])])
    res = M.h_race(ctx, {"unlock": "20=1", "target": "21=100", "window": 500})
    # unlock write first, target write second (order preserved).
    assert _addr_of(ctx.conns[0].sent) == 20
    assert _addr_of(ctx.conns[1].sent) == 21
    # target read back == expected -> observed state change, never reported as success.
    assert res.verify == Verify.STATE_CHANGED
    assert not res.success
    assert "window 500 ms: met" in res.detail
    assert "unlock" in res.extra["operations"] and "target" in res.extra["operations"]


def test_race_unsuccessful_verification_is_not_success():
    # target reads back a DIFFERENT value -> the write did not stick.
    ctx = FakeCtx(tcp_responses=[mb_write_echo(20, 1), mb_write_echo(21, 100), mb_read_resp([7])])
    res = M.h_race(ctx, {"unlock": "20=1", "target": "21=100", "window": 500})
    assert res.verify in (Verify.ACKED, Verify.SENT)
    assert not res.success
    assert "NOT observed" in res.response


def test_race_each_write_runs_once_no_retry():
    ctx = FakeCtx(tcp_responses=[mb_write_echo(20, 1), mb_write_echo(21, 100), mb_read_resp([100])])
    M.h_race(ctx, {"unlock": "20=1", "target": "21=100", "window": 500})
    # exactly two writes + one verify read == three connections used (no extra retry attempts).
    assert len(ctx.conns) == 3


def test_race_transport_failure_reported_not_crash():
    ctx = FakeCtx()

    def boom(proto):
        raise TransportError("no route")
    ctx.tcp = boom
    res = M.h_race(ctx, {"unlock": "20=1", "target": "21=100", "window": 500})
    # the sequence engine records the write failures; nothing is claimed solved.
    assert not res.success
    assert res.verify in (Verify.SENT, Verify.ACKED)
    assert "no route" in res.extra["operations"]
