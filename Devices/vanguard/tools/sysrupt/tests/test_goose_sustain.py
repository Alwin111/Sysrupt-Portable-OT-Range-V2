# SPDX-License-Identifier: Apache-2.0
"""Tests for `goose sustain` (Feature 3): parameter validation, duration bounds, sqNum progression,
interruption, send failure, verification, and dry-run. No real sockets."""
import pytest

from sysrupt.modules import goose as G
from sysrupt.result import Verify
from _fakes import FakeCtx, FakeUdp, FailingUdp


@pytest.fixture(autouse=True)
def _no_sleep(monkeypatch):
    monkeypatch.setattr(G.time, "sleep", lambda s: None)


def _ns(**kw):
    base = {"appid": 0x0bb8, "stnum": 6, "confrev": 1, "duration": 3, "interval": 1.0,
            "open": True, "test": False, "flags": None, "dry_run": False}
    base.update(kw)
    return base


@pytest.mark.parametrize("kw", [
    {"appid": -1}, {"appid": 0x10000}, {"stnum": -1}, {"confrev": 0x10000},
])
def test_parameter_validation(kw):
    ctx = FakeCtx(udp=FakeUdp())
    with pytest.raises(ValueError):
        G.h_sustain(ctx, _ns(**kw))


@pytest.mark.parametrize("dur", [0, -1, 31])
def test_duration_bounds(dur):
    ctx = FakeCtx(udp=FakeUdp())
    with pytest.raises(ValueError):
        G.h_sustain(ctx, _ns(duration=dur))


def test_sqnum_progression_and_fixed_stnum():
    udp = FakeUdp(reply=None)
    ctx = FakeCtx(udp=udp)
    res = G.h_sustain(ctx, _ns(duration=3, interval=1.0, open=True))
    assert len(udp.sent) >= 2
    decoded = [G.decode_frame(f) for f in udp.sent]
    assert [d["sqNum"] for d in decoded] == list(range(len(decoded)))   # 0,1,2,... progression
    assert all(d["stNum"] == 6 for d in decoded)                       # stNum fixed
    assert all(d["appid"] == 0x0bb8 and d["confRev"] == 1 for d in decoded)
    assert all(d["breaker_open"] for d in decoded)                     # --open honoured
    assert not res.success                                            # transmitting is never success


def test_interruption_stops_cleanly():
    udp = FakeUdp(rx_frames=[], raise_after=True)
    ctx = FakeCtx(udp=udp)
    res = G.h_sustain(ctx, _ns(duration=5))
    assert len(udp.sent) >= 1
    assert res.verify == Verify.SENT and not res.success
    assert "interrupted" in res.response


def test_send_failure_reported_as_error():
    ctx = FakeCtx(udp=FailingUdp())
    res = G.h_sustain(ctx, _ns(duration=5))
    assert res.verify == Verify.ERROR and not res.success


def test_flag_in_reply_is_completion():
    ctx = FakeCtx(udp=FakeUdp(reply=b"ACCEPTED flag{t3st}"))
    res = G.h_sustain(ctx, _ns(duration=5))
    assert res.verify == Verify.COMPLETED and res.flag == "flag{t3st}" and res.success


def test_accepted_without_flag_is_acked_not_success():
    ctx = FakeCtx(udp=FakeUdp(reply=b"ACCEPTED"))
    res = G.h_sustain(ctx, _ns(duration=2, interval=1.0))
    assert res.verify == Verify.ACKED and not res.success


def test_no_reply_is_sent_not_success():
    ctx = FakeCtx(udp=FakeUdp(reply=None))
    res = G.h_sustain(ctx, _ns(duration=2, interval=1.0))
    assert res.verify == Verify.SENT and not res.success


def test_dry_run_transmits_nothing():
    ctx = FakeCtx()                                   # ctx.udp must NOT be called
    res = G.h_sustain(ctx, _ns(duration=3, dry_run=True))
    assert res.verify == Verify.SENT and not res.success
    assert "dry-run" in res.detail.lower()
    assert ctx.udp_conns == []                        # nothing opened, nothing sent
    assert "sqNum=0" in res.extra["preview"]
