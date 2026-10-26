# SPDX-License-Identifier: Apache-2.0
import json

import pytest

from sysrupt.result import OperationResult, Verify
from sysrupt.timing import SequenceError, load_sequence, run_sequence


def _write(tmp_path, doc):
    p = tmp_path / "seq.json"
    p.write_text(json.dumps(doc))
    return str(p)


def _ok(name="op"):
    return OperationResult("T", name, "t", verify=Verify.ACKED, response="ok")


def test_load_string_and_object_steps(tmp_path):
    p = _write(tmp_path, {"name": "s", "steps": [
        "modbus write 20 1",
        {"command": "modbus write 21 100", "deadline_ms": 2000, "name": "commit"},
    ]})
    name, desc, steps, _ = load_sequence(p)
    assert name == "s" and len(steps) == 2
    assert steps[1].name == "commit" and steps[1].deadline_ms == 2000


def test_load_rejects_malformed(tmp_path):
    with pytest.raises(SequenceError):
        load_sequence(_write(tmp_path, {"no_steps": True}))
    with pytest.raises(SequenceError):
        load_sequence(_write(tmp_path, {"steps": []}))


def test_runs_in_order_and_once_each():
    calls = []

    def ex(tokens):
        calls.append(" ".join(tokens))
        return _ok(tokens[-1])

    _, _, steps, _ = load_sequence_from([
        "a 1", "b 2", "c 3",
    ])
    report = run_sequence("s", steps, executor=ex, sleep=lambda s: None)
    assert calls == ["a 1", "b 2", "c 3"]          # ordered
    assert len(report.steps) == 3                   # each once -- no retries
    assert report.all_ok


def load_sequence_from(cmds):
    from sysrupt.timing import Step
    return "s", "", [Step(command=c) for c in cmds], 0


def test_delays_use_injected_sleep():
    slept = []
    from sysrupt.timing import Step
    steps = [Step(command="x", delay_before_ms=100, delay_after_ms=50)]
    run_sequence("s", steps, executor=lambda t: _ok(), sleep=lambda s: slept.append(s))
    assert slept == [0.1, 0.05]


def test_cancellation_stops_before_step():
    calls = []
    from sysrupt.timing import Step
    steps = [Step(command="a"), Step(command="b")]
    state = {"n": 0}

    def cancel():
        state["n"] += 1
        return state["n"] > 1      # allow first step, cancel before second

    rep = run_sequence("s", steps, executor=lambda t: calls.append(t) or _ok(),
                       cancel=cancel, sleep=lambda s: None)
    assert len(calls) == 1 and rep.cancelled


def test_verify_assertion_pass_and_fail():
    from sysrupt.timing import Step

    def ex(tokens):
        r = _ok()
        r.response = "last_authorised = 100"
        return r

    passing = run_sequence("s", [Step(command="x", verify="contains:100")],
                           executor=ex, sleep=lambda s: None)
    assert passing.steps[0].verify_ok is True and passing.all_ok

    failing = run_sequence("s", [Step(command="x", verify="contains:999")],
                           executor=ex, sleep=lambda s: None)
    assert failing.steps[0].verify_ok is False and not failing.all_ok


def test_bad_command_string_records_error_not_crash():
    from sysrupt.timing import Step

    def ex(tokens):
        raise ValueError("boom")

    rep = run_sequence("s", [Step(command="bad")], executor=ex, sleep=lambda s: None)
    assert rep.steps[0].error == "boom" and not rep.steps[0].ok
