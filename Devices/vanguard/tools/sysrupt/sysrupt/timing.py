# SPDX-License-Identifier: Apache-2.0
"""The optional timing / sequence engine.

Timing is strictly opt-in: ordinary protocol commands never touch this module. A sequence is an
explicit, ordered list of steps supplied by the player in a JSON file. Each step is a literal
sysrupt command string; the engine runs them in order through the SAME command handlers, with
optional per-step delays and deadlines, measured on a monotonic clock, reusing one persistent
connection per protocol where the steps need it (e.g. a Modbus unlock+write race).

The engine executes ONLY the steps given. It never infers a register, key, flag, originator or
target value, and never silently retries a state-changing step. Partial success is reported
explicitly, step by step.
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

from .result import OperationResult, Verify


class SequenceError(Exception):
    """A malformed or invalid sequence file."""


@dataclass
class Step:
    command: str                       # a full sysrupt command line, e.g. "modbus write 20 1"
    name: str = ""
    delay_before_ms: int = 0
    delay_after_ms: int = 0
    deadline_ms: int = 0               # 0 == no deadline
    verify: str = ""                   # optional read-only assertion, e.g. "contains:100"

    @property
    def label(self) -> str:
        return self.name or self.command


@dataclass
class StepResult:
    step: Step
    result: Optional[OperationResult]
    elapsed_ms: float
    deadline_exceeded: bool = False
    verify_ok: Optional[bool] = None   # None == no assertion was requested
    error: str = ""

    @property
    def ok(self) -> bool:
        if self.error:
            return False
        if self.verify_ok is False:
            return False
        return True


@dataclass
class SequenceReport:
    name: str
    steps: list = field(default_factory=list)   # list[StepResult]
    cancelled: bool = False
    total_ms: float = 0.0

    @property
    def all_ok(self) -> bool:
        return not self.cancelled and all(s.ok for s in self.steps) and bool(self.steps)

    def format(self) -> str:
        lines = [f"Sequence: {self.name}  ({len(self.steps)} steps, {self.total_ms:.1f} ms total)"]
        for i, s in enumerate(self.steps, 1):
            status = "OK" if s.ok else "FAIL"
            tags = []
            if s.deadline_exceeded:
                tags.append("DEADLINE-EXCEEDED")
            if s.verify_ok is True:
                tags.append("verify:pass")
            elif s.verify_ok is False:
                tags.append("verify:FAIL")
            if s.error:
                tags.append(f"error:{s.error}")
            verify_lvl = s.result.verify.label if s.result else "-"
            lines.append(f"  [{i}] {status:4}  {s.step.label}  ({s.elapsed_ms:.1f} ms)  "
                         f"-> {verify_lvl}" + (("  [" + ", ".join(tags) + "]") if tags else ""))
        if self.cancelled:
            lines.append("  (cancelled before completion)")
        lines.append(f"Result: {'ALL STEPS OK' if self.all_ok else 'PARTIAL / SEE ABOVE'}")
        return "\n".join(lines)


def load_sequence(path: str) -> tuple[str, str, list, int]:
    """Parse and validate a sequence file. Returns (name, description, steps, default_delay_ms)."""
    try:
        with open(path, "r", encoding="utf-8") as fh:
            doc = json.load(fh)
    except FileNotFoundError as e:
        raise SequenceError(f"sequence file not found: {path}") from e
    except json.JSONDecodeError as e:
        raise SequenceError(f"invalid JSON in {path}: {e}") from e
    if not isinstance(doc, dict) or "steps" not in doc or not isinstance(doc["steps"], list):
        raise SequenceError("sequence must be a JSON object with a 'steps' array")
    if not doc["steps"]:
        raise SequenceError("sequence has no steps")
    if len(doc["steps"]) > 64:
        raise SequenceError("too many steps (max 64)")
    default_delay = int(doc.get("default_delay_ms", 0))
    steps = []
    for n, raw in enumerate(doc["steps"], 1):
        if isinstance(raw, str):
            steps.append(Step(command=raw))
            continue
        if not isinstance(raw, dict) or "command" not in raw:
            raise SequenceError(f"step {n}: must be a string or an object with a 'command'")
        steps.append(Step(
            command=str(raw["command"]),
            name=str(raw.get("name", "")),
            delay_before_ms=int(raw.get("delay_before_ms", 0)),
            delay_after_ms=int(raw.get("delay_after_ms", default_delay)),
            deadline_ms=int(raw.get("deadline_ms", 0)),
            verify=str(raw.get("verify", "")),
        ))
    return str(doc.get("name", path)), str(doc.get("description", "")), steps, default_delay


def _check_verify(expr: str, res: Optional[OperationResult]) -> Optional[bool]:
    """Evaluate an explicit, read-only assertion against a result. None if no assertion."""
    if not expr:
        return None
    if res is None:
        return False
    text = f"{res.response} {res.detail} {res.extra}"
    if expr.startswith("contains:"):
        return expr[len("contains:"):] in text
    if expr.startswith("verify>="):
        try:
            return int(res.verify) >= int(expr[len("verify>="):])
        except ValueError:
            return False
    if expr.startswith("verify="):
        name = expr[len("verify="):].upper()
        return res.verify.name == name
    # Unknown assertion form: treat as not satisfied rather than silently passing.
    return False


def run_sequence(name: str, steps: list,
                 executor: Callable[[list], OperationResult],
                 on_step: Optional[Callable] = None,
                 cancel: Optional[Callable[[], bool]] = None,
                 sleep: Callable[[float], None] = time.sleep) -> SequenceReport:
    """Execute steps in order. `executor(tokens)` runs one command and returns an OperationResult.

    `cancel()` is polled between steps; `sleep` is injectable for tests. State-changing steps are
    never retried: each step runs exactly once.
    """
    report = SequenceReport(name=name)
    t0 = time.monotonic()
    for step in steps:
        if cancel and cancel():
            report.cancelled = True
            break
        if step.delay_before_ms > 0:
            sleep(step.delay_before_ms / 1000.0)
        s0 = time.monotonic()
        res: Optional[OperationResult] = None
        err = ""
        try:
            res = executor(step.command.split())
        except KeyboardInterrupt:
            report.cancelled = True
            report.steps.append(StepResult(step, None, (time.monotonic() - s0) * 1000.0,
                                            error="cancelled"))
            break
        except Exception as e:  # a bad command string / transport error must not abort silently
            err = str(e)
        elapsed = (time.monotonic() - s0) * 1000.0
        deadline_hit = bool(step.deadline_ms) and elapsed > step.deadline_ms
        verify_ok = _check_verify(step.verify, res)
        sr = StepResult(step, res, elapsed, deadline_hit, verify_ok, err)
        report.steps.append(sr)
        if on_step:
            on_step(len(report.steps), sr)
        if step.delay_after_ms > 0:
            sleep(step.delay_after_ms / 1000.0)
    report.total_ms = (time.monotonic() - t0) * 1000.0
    return report
