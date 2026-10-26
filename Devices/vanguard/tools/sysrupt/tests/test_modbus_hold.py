# SPDX-License-Identifier: Apache-2.0
"""Tests for `modbus hold` (Feature 4): already-satisfied, termination on condition, timeout,
read/write failure, invalid config, and the lab-target write gate. Faked transports only.

`hold` uses ONE persistent connection, so a FakeCtx serves the whole loop from a single concatenated
byte stream (the FakeTcp cursor walks it frame by frame)."""
import pytest

from sysrupt.modules import modbus as M
from sysrupt.result import Verify
from _fakes import FakeCtx, mb_read_resp, mb_write_echo


@pytest.fixture(autouse=True)
def _no_sleep(monkeypatch):
    monkeypatch.setattr(M.time, "sleep", lambda s: None)


def _stream(*frames):
    return FakeCtx(tcp_responses=[b"".join(frames)])


def test_already_satisfied_stops_immediately_no_write():
    ctx = _stream(mb_read_resp([5]))                      # condition already true
    res = M.h_hold(ctx, {"adjust": "73=100", "until": "74=5", "timeout": 10.0,
                         "interval": 0.5, "max_iters": 50})
    assert res.verify == Verify.STATE_CHANGED and not res.success
    assert "after 1 iter" in res.response
    # only one request was sent (the condition read), no write.
    assert ctx.conns[0].sent and len(ctx.conns) == 1


def test_terminates_when_condition_becomes_true():
    ctx = _stream(mb_read_resp([0]), mb_write_echo(73, 100), mb_read_resp([1]))
    res = M.h_hold(ctx, {"adjust": "73=100", "until": "74=1", "timeout": 10.0,
                         "interval": 0.5, "max_iters": 50})
    assert res.verify == Verify.STATE_CHANGED
    assert "after 2 iter" in res.response


def test_timeout_bounded_by_max_iters_is_not_success():
    ctx = _stream(mb_read_resp([0]), mb_write_echo(73, 100),
                  mb_read_resp([0]), mb_write_echo(73, 100))
    res = M.h_hold(ctx, {"adjust": "73=100", "until": "74=1", "timeout": 10.0,
                         "interval": 0.5, "max_iters": 2})
    assert res.verify == Verify.ACKED and not res.success
    assert "NOT observed" in res.response


def test_read_failure_aborts_with_error():
    ctx = _stream(b"")                                   # no bytes -> short MBAP -> ModbusError
    res = M.h_hold(ctx, {"adjust": "73=100", "until": "74=1", "timeout": 10.0,
                         "interval": 0.5, "max_iters": 5})
    assert res.verify == Verify.ERROR and "stopped on error" in res.response


def test_write_failure_aborts_with_error():
    # condition read OK (!=), then the write has no response -> error, loop stops (no infinite retry).
    ctx = _stream(mb_read_resp([0]))
    res = M.h_hold(ctx, {"adjust": "73=100", "until": "74=1", "timeout": 10.0,
                         "interval": 0.5, "max_iters": 5})
    assert res.verify == Verify.ERROR


def test_until_input_reads_input_register():
    ctx = _stream(mb_read_resp([9], fc=4))
    res = M.h_hold(ctx, {"adjust": "73=100", "until": "74=9", "until_input": True,
                         "timeout": 10.0, "interval": 0.5, "max_iters": 5})
    assert res.verify == Verify.STATE_CHANGED
    assert "IR74" in res.request


@pytest.mark.parametrize("kw", [
    {"adjust": "bad", "until": "74=1"},
    {"adjust": "73=100", "until": "nope"},
    {"adjust": "73=100", "until": "74=1", "timeout": 0},
    {"adjust": "73=100", "until": "74=1", "timeout": 999},
])
def test_invalid_config_rejected(kw):
    ctx = _stream(mb_read_resp([0]))
    with pytest.raises(M.ModbusError):
        M.h_hold(ctx, kw)


def test_write_gate_refuses_non_lab_target():
    ctx = FakeCtx(target="8.8.8.8", tcp_responses=[mb_read_resp([0])])
    with pytest.raises(M.ModbusError):
        M.h_hold(ctx, {"adjust": "73=100", "until": "74=1", "timeout": 10.0,
                       "interval": 0.5, "max_iters": 5})


def test_lab_target_helper():
    assert M._is_lab_target("10.0.1.1") and M._is_lab_target("127.0.0.1")
    assert M._is_lab_target("192.168.1.5") and not M._is_lab_target("8.8.8.8")
    assert not M._is_lab_target("example.com")
