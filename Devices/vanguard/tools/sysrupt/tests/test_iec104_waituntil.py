# SPDX-License-Identifier: Apache-2.0
"""Tests for `iec104 wait-until` (Feature 2): immediate success, delayed success, timeout, malformed
input, disconnects / protocol errors. Read-only: it must never transmit a control command."""
import pytest

from sysrupt.modules import iec104 as I
from sysrupt.result import Verify
from sysrupt.transport import TransportError
from _fakes import FakeCtx, iec_interro


@pytest.fixture(autouse=True)
def _no_sleep(monkeypatch):
    monkeypatch.setattr(I.time, "sleep", lambda s: None)


def _clock(monkeypatch, step=0.4):
    t = {"v": 0.0}

    def now():
        v = t["v"]
        t["v"] += step
        return v
    monkeypatch.setattr(I.time, "monotonic", now)


def test_immediate_success_is_state_changed_not_solved():
    ctx = FakeCtx(tcp_responses=[iec_interro(1001, 0)])
    res = I.h_wait_until(ctx, {"point": 1001, "value": 0, "timeout": 30.0, "interval": 0.5})
    assert res.verify == Verify.STATE_CHANGED and not res.success
    assert "after 1 poll" in res.response


def test_delayed_success():
    ctx = FakeCtx(tcp_responses=[iec_interro(1001, 1), iec_interro(1001, 0)])
    res = I.h_wait_until(ctx, {"point": 1001, "value": 0, "timeout": 30.0, "interval": 0.5})
    assert res.verify == Verify.STATE_CHANGED
    assert "after 2 poll" in res.response


def test_wait_until_is_read_only_sends_no_control():
    ctx = FakeCtx(tcp_responses=[iec_interro(1001, 0)])
    I.h_wait_until(ctx, {"point": 1001, "value": 0, "timeout": 30.0, "interval": 0.5})
    # the only bytes sent are STARTDT + C_IC interrogation -- never a C_SC control (type 45).
    sent = ctx.conns[0].sent
    assert bytes([45]) not in sent               # no C_SC type id transmitted
    assert sent.startswith(bytes([0x68, 0x04, 0x07]))  # STARTDT act


def test_timeout_returns_not_solved(monkeypatch):
    _clock(monkeypatch, step=0.4)
    ctx = FakeCtx(tcp_responses=[iec_interro(1001, 1), iec_interro(1001, 1)])
    res = I.h_wait_until(ctx, {"point": 1001, "value": 0, "timeout": 1.0, "interval": 0.3})
    assert res.verify == Verify.ACKED and not res.success
    assert "timeout" in res.response


@pytest.mark.parametrize("timeout", [0, -1, 301])
def test_malformed_timeout_rejected(timeout):
    ctx = FakeCtx()
    with pytest.raises(ValueError):
        I.h_wait_until(ctx, {"point": 1001, "value": 0, "timeout": timeout, "interval": 0.5})


def test_disconnects_bounded_no_infinite_retry():
    ctx = FakeCtx()

    def boom(proto):
        raise TransportError("connection reset")
    ctx.tcp = boom
    res = I.h_wait_until(ctx, {"point": 1001, "value": 0, "timeout": 30.0, "interval": 0.5})
    assert res.verify == Verify.ERROR and not res.success
    assert "connection reset" in res.detail
