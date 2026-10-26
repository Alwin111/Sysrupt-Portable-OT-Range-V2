# SPDX-License-Identifier: Apache-2.0
"""Tests for `tristation dump --watch --diff` (Feature 5): no change, one/multiple changes, missing
entries (never treated as 0), invalid config, transport errors, and plain-dump preservation."""
import pytest

from sysrupt.modules import tristation as T
from sysrupt.result import Verify
from sysrupt.transport import TransportError
from _fakes import FakeCtx, ts_read_resp


@pytest.fixture(autouse=True)
def _no_sleep(monkeypatch):
    monkeypatch.setattr(T.time, "sleep", lambda s: None)


def _ns(start=0, count=2, watch=True, diff=True, interval=1.0):
    return {"start": start, "count": count, "watch": watch, "diff": diff, "interval": interval}


def test_plain_dump_preserved_without_watch():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10), ts_read_resp(1, 20)])
    res = T.h_dump(ctx, _ns(watch=False, diff=False))
    assert res.operation == "dump" and res.verify == Verify.ACKED
    assert "dump" in res.extra and "reg 0: 10" in res.extra["dump"]


def test_watch_no_changes():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10), ts_read_resp(1, 20),
                                 ts_read_resp(0, 10), ts_read_resp(1, 20)])
    res = T.h_dump(ctx, _ns())
    assert res.operation == "dump (watch)"
    assert res.response == "0 changed, 0 appeared, 0 disappeared"


def test_watch_one_changed_shows_old_and_new():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10), ts_read_resp(1, 20),
                                 ts_read_resp(0, 10), ts_read_resp(1, 99)])
    res = T.h_dump(ctx, _ns())
    assert "1 changed" in res.response
    assert "reg 1: 20 -> 99" in res.extra["diff"]


def test_watch_multiple_changes():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10), ts_read_resp(1, 20),
                                 ts_read_resp(0, 11), ts_read_resp(1, 99)])
    res = T.h_dump(ctx, _ns())
    assert "2 changed" in res.response


def test_watch_appeared_and_disappeared():
    # reg1 unreadable in snap1 (b""), readable in snap2 -> appeared.
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10), b"",
                                 ts_read_resp(0, 10), ts_read_resp(1, 5)])
    res = T.h_dump(ctx, _ns())
    assert "appeared" in res.extra["diff"] and "reg 1: (no resp) -> 5" in res.extra["diff"]

    ctx2 = FakeCtx(tcp_responses=[ts_read_resp(0, 10), ts_read_resp(1, 5),
                                  ts_read_resp(0, 10), b""])
    res2 = T.h_dump(ctx2, _ns())
    assert "reg 1: 5 -> (no resp)" in res2.extra["diff"]


def test_watch_unreadable_in_both_is_not_zero():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10), b"", ts_read_resp(0, 10), b""])
    res = T.h_dump(ctx, _ns())
    assert "unreadable" in res.extra["diff"] and "not treated as 0" in res.extra["diff"]
    assert "reg 1: 0" not in res.extra["diff"]        # never invented a zero
    assert res.response == "0 changed, 0 appeared, 0 disappeared"


def test_diff_without_watch_is_rejected():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 10)])
    with pytest.raises(ValueError):
        T.h_dump(ctx, _ns(watch=False, diff=True))


def test_count_out_of_range_rejected():
    ctx = FakeCtx()
    with pytest.raises(ValueError):
        T.h_dump(ctx, _ns(count=99))


def test_transport_error_makes_registers_unreadable_not_zero():
    ctx = FakeCtx()

    def boom(proto):
        raise TransportError("link down")
    ctx.new_tcp = boom
    res = T.h_dump(ctx, _ns(count=2))
    assert res.verify == Verify.ACKED                 # read-only; handled, not crashed
    assert res.response == "0 changed, 0 appeared, 0 disappeared"
    assert "unreadable" in res.extra["diff"]
