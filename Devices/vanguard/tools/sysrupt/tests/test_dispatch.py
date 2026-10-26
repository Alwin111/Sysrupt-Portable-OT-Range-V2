# SPDX-License-Identifier: Apache-2.0
"""End-to-end dispatch tests: real Context + registry, with TCP mocked at ctx.tcp."""
from sysrupt.cli import build_registry, dispatch
from sysrupt.config import Config
from sysrupt.context import Context
from sysrupt.history import History
from _fakes import FakeTcp, mb_read_resp, mb_write_echo


def _ctx(tmp_path, responses):
    cfg = Config.load(str(tmp_path / "c.json"))
    ctx = Context(cfg, history=History(str(tmp_path / "h.jsonl")))
    ctx.registry = build_registry()
    queue = list(responses)

    def fake_tcp(proto):
        return FakeTcp(queue.pop(0) if queue else b"")
    ctx.tcp = fake_tcp
    ctx.new_tcp = fake_tcp
    return ctx


def test_dispatch_version_and_help(tmp_path, capsys):
    ctx = _ctx(tmp_path, [])
    assert dispatch(ctx, ["version"]) == 0
    assert "sysrupt" in capsys.readouterr().out
    assert dispatch(ctx, ["help"]) == 0
    assert "Command groups" in capsys.readouterr().out


def test_dispatch_unknown_command_rc2(tmp_path, capsys):
    ctx = _ctx(tmp_path, [])
    assert dispatch(ctx, ["frobnicate"]) == 2
    assert "unknown command" in capsys.readouterr().err


def test_dispatch_modbus_read_records_history(tmp_path, capsys):
    ctx = _ctx(tmp_path, [mb_read_resp([1, 10], fc=3)])
    rc = dispatch(ctx, ["modbus", "read-holding", "20", "2"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "Protocol acknowledgement" in out
    assert ctx.history.count() == 1


def test_dispatch_write_prints_ack_not_success(tmp_path, capsys):
    ctx = _ctx(tmp_path, [mb_write_echo(20, 1)])
    dispatch(ctx, ["modbus", "write", "20", "1"])
    out = capsys.readouterr().out
    assert "acknowledg" in out.lower()
    assert "SUCCESS" not in out.upper()       # never claims success on an ack


def test_dispatch_missing_arg_rc2(tmp_path, capsys):
    ctx = _ctx(tmp_path, [])
    assert dispatch(ctx, ["modbus", "write", "20"]) == 2
    assert "missing required" in capsys.readouterr().err
