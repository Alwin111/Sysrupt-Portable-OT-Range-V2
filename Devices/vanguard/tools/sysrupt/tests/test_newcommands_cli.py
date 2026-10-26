# SPDX-License-Identifier: Apache-2.0
"""Registry/CLI integration for the five new commands, plus the option-key normalization
(dashes -> underscores) that also makes the pre-existing `--no-timing` flag actually take effect."""
import pytest

from sysrupt.cli import build_registry, _h_help


def test_new_commands_resolve_and_parse():
    reg = build_registry()
    specs = [
        (["modbus", "race", "20=1", "21=100", "--window", "500"], {"window": 500}),
        (["iec104", "wait-until", "1001", "0"], {"point": 1001, "value": 0}),
        (["goose", "sustain", "--appid", "0x0bb8", "--stnum", "6", "--confrev", "1", "--duration", "5"],
         {"appid": 0x0bb8, "duration": 5}),
        (["tristation", "dump", "0", "8", "--watch", "--diff"], {"watch": True, "diff": True}),
    ]
    for toks, expect in specs:
        cmd, rest = reg.resolve(toks)
        ns = cmd.parse(rest)
        for k, v in expect.items():
            assert ns[k] == v


def test_hyphenated_option_keys_normalized_to_underscores():
    reg = build_registry()
    cmd, rest = reg.resolve(["modbus", "hold", "73=100", "--until", "74=1",
                             "--until-input", "--max-iters", "5"])
    ns = cmd.parse(rest)
    assert ns["adjust"] == "73=100" and ns["until"] == "74=1"
    assert ns["until_input"] is True and ns["max_iters"] == 5


def test_goose_sustain_dry_run_key_normalized():
    reg = build_registry()
    cmd, rest = reg.resolve(["goose", "sustain", "--appid", "1", "--stnum", "1",
                             "--confrev", "1", "--duration", "2", "--dry-run"])
    ns = cmd.parse(rest)
    assert ns["dry_run"] is True


def test_existing_no_timing_flag_now_takes_effect():
    # Pre-existing latent bug: `_h_sequence_run` reads ns["no_timing"] but the key used to be
    # "no-timing". Normalization fixes it without changing any command's visible syntax.
    reg = build_registry()
    cmd, rest = reg.resolve(["sequence", "run", "seq.json", "--no-timing"])
    ns = cmd.parse(rest)
    assert ns["no_timing"] is True


def test_unknown_option_message_keeps_dashes():
    reg = build_registry()
    cmd, rest = reg.resolve(["modbus", "hold", "73=100", "--until", "74=1", "--bogus-opt"])
    from sysrupt.registry import CommandError
    with pytest.raises(CommandError, match="bogus-opt"):
        cmd.parse(rest)


def test_help_lists_new_commands_and_marks_state_changing():
    reg = build_registry()
    ctx = type("C", (), {"registry": reg})()
    modbus_help = _h_help(ctx, {"topic": "modbus"})
    assert "race" in modbus_help and "hold" in modbus_help and "STATE-CHANGING" in modbus_help
    assert "wait-until" in _h_help(ctx, {"topic": "iec104"})
    assert "sustain" in _h_help(ctx, {"topic": "goose"})
    assert "--watch" in _h_help(ctx, {"topic": "tristation"})
