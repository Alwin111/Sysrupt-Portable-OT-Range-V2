# SPDX-License-Identifier: Apache-2.0
from sysrupt.modules import iec104 as I
from sysrupt.result import Verify
from _fakes import FakeCtx, STARTDT_CON, srv_i, iec_flag_frames


def test_frame_roundtrip():
    f = I.client_i(45, 6, 7, 1, 1001, bytes([0]))
    assert f[0] == 0x68 and f[6] == 45
    # a server single-point frame for IOA 1001 value 1
    buf = srv_i(1, 20, 0, 1, 1001, bytes([1]))
    assert I.find_ioa(buf, 1001) == 1


def test_status_startdt_con():
    ctx = FakeCtx(tcp_responses=[STARTDT_CON])
    res = I.h_status(ctx, {})
    assert res.verify == Verify.ACKED and "STARTDT_CON" in res.detail


def test_interrogate_lists_points():
    frames = srv_i(1, 20, 0, 1, 1001, bytes([0])) + srv_i(13, 20, 0, 1, 2003, b"\x07\x00")
    ctx = FakeCtx(tcp_responses=[STARTDT_CON + frames])
    res = I.h_interrogate(ctx, {})
    assert res.verify == Verify.ACKED and "IOA 1001" in res.extra["points"]
    assert "IOA 2003" in res.extra["points"]


def test_command_positive_actcon_is_ack_not_state_change():
    actcon = srv_i(45, 7, 7, 1, 1001, bytes([0]))          # COT 7, no P/N bit
    ctx = FakeCtx(tcp_responses=[STARTDT_CON + actcon])
    res = I.h_command(ctx, {"ioa": 1001, "value": 0, "originator": 7})
    assert res.verify == Verify.ACKED
    assert not res.success
    assert "not prove" in res.detail.lower()
    # the frame was printed in the request field (shown before transmission)
    assert "frame=" in res.request


def test_command_negative_actcon_rejected():
    neg = srv_i(45, 0x47, 99, 1, 1001, bytes([0]))          # P/N bit set
    ctx = FakeCtx(tcp_responses=[STARTDT_CON + neg])
    res = I.h_command(ctx, {"ioa": 1001, "value": 1, "originator": 99})
    assert res.verify == Verify.SENT and "NEGATIVE" in res.detail


def test_flag_completed_from_device_output():
    frames = iec_flag_frames("flag{1ec104_demo}")
    ctx = FakeCtx(tcp_responses=[STARTDT_CON + frames])
    res = I.h_flag(ctx, {})
    assert res.verify == Verify.COMPLETED and res.flag == "flag{1ec104_demo}" and res.success


def test_flag_empty_not_solved():
    frames = iec_flag_frames("")      # all zero objects
    ctx = FakeCtx(tcp_responses=[STARTDT_CON + frames])
    res = I.h_flag(ctx, {})
    assert res.verify == Verify.ACKED and res.flag is None and not res.success
