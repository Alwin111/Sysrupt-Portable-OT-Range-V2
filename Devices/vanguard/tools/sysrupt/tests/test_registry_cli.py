# SPDX-License-Identifier: Apache-2.0
import pytest

from sysrupt.cli import build_registry, _h_help, _h_version
from sysrupt.registry import ArgSpec, Command, CommandError, parse_int


def test_parse_int_forms():
    assert parse_int("10") == 10
    assert parse_int("0x64") == 100
    assert parse_int("0b101") == 5
    with pytest.raises(CommandError):
        parse_int("nope")


def test_resolve_and_parse_positionals():
    reg = build_registry()
    cmd, rest = reg.resolve(["modbus", "read-holding", "20", "2"])
    ns = cmd.parse(rest)
    assert ns["addr"] == 20 and ns["count"] == 2


def test_parse_flags_and_options():
    reg = build_registry()
    cmd, rest = reg.resolve(["modbus", "dump", "48", "16", "--input"])
    ns = cmd.parse(rest)
    assert ns["start"] == 48 and ns["count"] == 16 and ns["input"] is True


def test_missing_required_argument():
    reg = build_registry()
    cmd, rest = reg.resolve(["modbus", "write", "20"])
    with pytest.raises(CommandError):
        cmd.parse(rest)


def test_unknown_option_rejected():
    reg = build_registry()
    cmd, rest = reg.resolve(["modbus", "read-holding", "20", "--bogus"])
    with pytest.raises(CommandError):
        cmd.parse(rest)


def test_unknown_command_and_subcommand():
    reg = build_registry()
    with pytest.raises(CommandError):
        reg.resolve(["frobnicate"])
    with pytest.raises(CommandError):
        reg.resolve(["modbus", "nope"])


def test_group_default_subcommand():
    reg = build_registry()
    cmd, rest = reg.resolve(["history"])         # bare group -> default 'list'
    assert cmd.name == "list" and rest == []


def test_unavailable_command_errors_on_parse():
    c = Command("planned", "planned feature", lambda ctx, ns: None, available=False)
    with pytest.raises(CommandError):
        c.parse([])


def test_help_overview_and_group(capctx=None):
    reg = build_registry()
    ctx = type("C", (), {"registry": reg})()
    overview = _h_help(ctx, {"topic": None})
    assert "modbus" in overview and "iec104" in overview and "goose" in overview
    grp = _h_help(ctx, {"topic": "modbus"})
    assert "read-holding" in grp and "STATE-CHANGING" in grp   # write is marked
    with pytest.raises(CommandError):
        _h_help(ctx, {"topic": "does-not-exist"})


def test_version_handler():
    assert _h_version(None, {}).startswith("sysrupt ")


def test_hex_int_coercion_in_args():
    reg = build_registry()
    cmd, rest = reg.resolve(["iec104", "command", "1001", "0", "--originator", "0x07"])
    ns = cmd.parse(rest)
    assert ns["ioa"] == 1001 and ns["value"] == 0 and ns["originator"] == 7
