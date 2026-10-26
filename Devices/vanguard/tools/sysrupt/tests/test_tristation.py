# SPDX-License-Identifier: Apache-2.0
from sysrupt.modules import tristation as T
from sysrupt.result import Verify
from _fakes import FakeCtx


def ts_read_resp(reg, val):
    return bytes([0xAA, 0x55, 0x01, reg >> 8, reg & 0xFF, val >> 8, val & 0xFF])


def ts_status(fc, status):
    return bytes([0xAA, 0x55, fc, status])


def test_read_acked():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(42, 0x1A2B)])
    res = T.h_read(ctx, {"reg": 42})
    assert res.verify == Verify.ACKED and "0x1a2b" in res.response


def test_write_ok_is_ack_not_completion():
    ctx = FakeCtx(tcp_responses=[ts_status(0x02, 0)])
    res = T.h_write(ctx, {"reg": 1, "value": 1})
    assert res.verify == Verify.ACKED and not res.success


def test_write_rejected():
    ctx = FakeCtx(tcp_responses=[ts_status(0x02, 1)])
    res = T.h_write(ctx, {"reg": 0, "value": 0})
    assert res.verify == Verify.SENT and "REJECTED" in res.detail


def test_download_accepted_is_ack():
    ctx = FakeCtx(tcp_responses=[ts_status(0x10, 0)])
    res = T.h_download(ctx, {"fw": 3008, "key": 0x1234, "trip": 0})
    assert res.verify == Verify.ACKED and not res.success


def test_download_bad_fw():
    ctx = FakeCtx(tcp_responses=[ts_status(0x10, 2)])
    res = T.h_download(ctx, {"fw": 1234, "key": 0x1234, "trip": 0})
    assert res.verify == Verify.SENT and "bad firmware" in res.detail


def test_verify_match_state_changed():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 0)])
    res = T.h_verify(ctx, {"reg": 0, "expected": 0})
    assert res.verify == Verify.STATE_CHANGED and not res.success


def test_flag_completed_from_device():
    # 16 reads making up "flag{sis_demo}" then zeros
    text = "flag{sis_demo}"
    data = text.encode() + b"\x00" * (32 - len(text))
    words = [(data[i * 2] << 8) | data[i * 2 + 1] for i in range(16)]
    ctx = FakeCtx(tcp_responses=[ts_read_resp(100 + i, words[i]) for i in range(16)])
    res = T.h_flag(ctx, {"base": 100})
    assert res.verify == Verify.COMPLETED and res.flag == "flag{sis_demo}"


def test_each_op_uses_fresh_connection():
    ctx = FakeCtx(tcp_responses=[ts_read_resp(0, 1)])
    T.h_read(ctx, {"reg": 0})
    assert len(ctx.conns) == 1 and ctx.conns[0].closed  # opened and closed
