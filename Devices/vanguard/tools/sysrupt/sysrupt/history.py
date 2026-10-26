# SPDX-License-Identifier: Apache-2.0
"""Operation history: an append-only JSONL log of what was done and what was verified.

Records are deliberately minimal -- timestamp, target, protocol, operation, duration, a short
response summary and the verification level. Credentials, session cookies and flag values are
never written here.
"""
from __future__ import annotations

import json
import os
import time
from typing import Optional

from .result import OperationResult, Verify


def _state_path() -> str:
    env = os.environ.get("SYSRUPT_HISTORY")
    if env:
        return env
    base = os.environ.get("XDG_STATE_HOME") or os.path.join(os.path.expanduser("~"), ".local", "state")
    return os.path.join(base, "sysrupt", "history.jsonl")


class History:
    def __init__(self, path: Optional[str] = None):
        self.path = path or _state_path()

    def record(self, res: OperationResult) -> int:
        rec = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "target": res.target,
            "protocol": res.protocol,
            "operation": res.operation,
            "duration_ms": round(res.elapsed_ms, 1),
            "request": _short(res.request),
            "response": _short(res.response),
            "verify": int(res.verify),
            "verify_label": res.verify.label,
            # record only WHETHER a flag was recovered, never the flag value itself
            "flag_recovered": bool(res.flag),
        }
        try:
            os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
            with open(self.path, "a", encoding="utf-8") as fh:
                fh.write(json.dumps(rec) + "\n")
        except OSError:
            return -1
        return self.count()

    def _all(self) -> list:
        try:
            with open(self.path, "r", encoding="utf-8") as fh:
                out = []
                for line in fh:
                    line = line.strip()
                    if line:
                        try:
                            out.append(json.loads(line))
                        except json.JSONDecodeError:
                            continue
                return out
        except (FileNotFoundError, OSError):
            return []

    def count(self) -> int:
        return len(self._all())

    def recent(self, limit: int = 20) -> list:
        items = self._all()
        return list(enumerate(items, start=1))[-limit:]

    def show(self, idx: int) -> Optional[dict]:
        items = self._all()
        if 1 <= idx <= len(items):
            return items[idx - 1]
        return None

    def clear(self) -> None:
        try:
            os.remove(self.path)
        except OSError:
            pass


def _short(s: str, n: int = 120) -> str:
    s = s or ""
    return s if len(s) <= n else s[: n - 3] + "..."
