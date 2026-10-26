# SPDX-License-Identifier: Apache-2.0
import struct

from sysrupt.modules import modbus as M
from sysrupt.result import Verify
from _fakes import FakeCtx, mb_read_resp, mb_exc, mb_write_echo


def _regs_for(text):
    data = text.encode()
    if len(data) % 2:
        data += b"\x00"
    return [struct.unpack(">H", data[i:i + 2])[0] for i in range(0, len(data), 2)]


def test_codec_build_and_parse():
    frame = M.build_read(1, 1, 3, 20, 2)
    assert frame[7] == 3 and struct.unpack(">H", frame[8:10])[0] == 20
    kind, fc, payload = M.parse_pdu(bytes([3, 4, 0, 1, 0, 2]))
    assert kind == "ok" and M.regs_from_read(payload) == [1, 2]
    kind, fc, code = M.parse_pdu(bytes([0x83, 2]))
    assert kind == "exception" and code == 2


def test_read_holding_acked():
    ctx = FakeCtx(tcp_responses=[mb_read_resp([1, 10], fc=3)])
    res = M.h_read_holding(ctx, {"addr": 20, "count": 2})
    assert res.verify == Verify.ACKED and "[1, 10]" in res.response
    assert not res.success


def test_read_detects_flag_as_completed():
    regs = _regs_for("flag{t3st}")
    ctx = FakeCtx(tcp_responses=[mb_read_resp(regs, fc=4)])
    res = M.h_read_input(ctx, {"addr": 48, "count": len(regs)})
    assert res.verify == Verify.COMPLETED and res.flag == "flag{t3st}" and res.success


def test_write_ack_is_not_completion():
    ctx = FakeCtx(tcp_responses=[mb_write_echo(20, 1)])
    res = M.h_write(ctx, {"addr": 20, "value": 1})
    assert res.verify == Verify.ACKED
    assert not res.success
    assert "not" in res.detail.lower() and "ack" in res.detail.lower()


def test_write_rejected_exception():
    ctx = FakeCtx(tcp_responses=[mb_exc(6, 2)])
    res = M.h_write(ctx, {"addr": 21, "value": 100})
    assert res.verify == Verify.SENT and "REJECTED" in res.detail


def test_verify_matches_is_state_changed_not_completed():
    ctx = FakeCtx(tcp_responses=[mb_read_resp([100], fc=4)])
    res = M.h_verify(ctx, {"addr": 73, "expected": 100, "input": True})
    assert res.verify == Verify.STATE_CHANGED and not res.success


def test_verify_mismatch():
    ctx = FakeCtx(tcp_responses=[mb_read_resp([10], fc=4)])
    res = M.h_verify(ctx, {"addr": 73, "expected": 100, "input": True})
    assert res.verify == Verify.ACKED


def test_read_exception_is_acked_readonly():
    ctx = FakeCtx(tcp_responses=[mb_exc(3, 2)])
    res = M.h_read_holding(ctx, {"addr": 999, "count": 1})
    assert res.verify == Verify.ACKED and "exception" in res.response


def test_decode_offline():
    res = M.h_decode(None, {"hex": "01 90 00 64"})
    assert "[400, 100]" in res.extra["decode"]


def test_transaction_id_increments_and_frame_sent():
    ctx = FakeCtx(tcp_responses=[mb_read_resp([5], fc=3)])
    M.h_read_holding(ctx, {"addr": 1, "count": 1})
    assert ctx.conns[0].sent  # a request was actually transmitted
