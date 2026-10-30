# SPDX-License-Identifier: Apache-2.0
"""Configuration: a small, dependency-free JSON file so players never edit Python to change target.

Lookup order for the config file:
    1. $SYSRUPT_CONFIG if set
    2. ./sysrupt.json in the current directory
    3. ~/.config/sysrupt/config.json  (the default; created on first `config set`)

Only operational preferences are stored (target, timeouts, output). Credentials, session cookies
and flags are never persisted here.
"""
from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass, field


DEFAULTS = {
    "target": "10.0.1.1",
    "connect_timeout": 5.0,
    "read_timeout": 5.0,
    "retries": 1,            # bounded; 0 = single attempt. Never used to repeat state-changing writes.
    "output": "text",        # text | json
    "verbose": False,
    "color": True,
    # Per-protocol default ports (overridable; match the simulator).
    "ports": {
        "modbus": 502,
        "goose": 6180,
        "historian": 9000,
        "web": 80,
    },
}


def _config_path() -> str:
    env = os.environ.get("SYSRUPT_CONFIG")
    if env:
        return env
    if os.path.isfile(os.path.join(os.getcwd(), "sysrupt.json")):
        return os.path.join(os.getcwd(), "sysrupt.json")
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "sysrupt", "config.json")


@dataclass
class Config:
    data: dict = field(default_factory=lambda: json.loads(json.dumps(DEFAULTS)))
    path: str = ""

    @classmethod
    def load(cls, path: str | None = None) -> "Config":
        p = path or _config_path()
        cfg = cls(path=p)
        try:
            with open(p, "r", encoding="utf-8") as fh:
                loaded = json.load(fh)
            if isinstance(loaded, dict):
                cfg._merge(loaded)
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            pass  # fall back to defaults; never crash on a missing/garbled config
        return cfg

    def _merge(self, loaded: dict) -> None:
        for k, v in loaded.items():
            if k == "ports" and isinstance(v, dict):
                self.data["ports"].update(v)
            else:
                self.data[k] = v

    def save(self) -> str:
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        with open(self.path, "w", encoding="utf-8") as fh:
            json.dump(self.data, fh, indent=2)
        return self.path

    # ---- typed accessors ----
    @property
    def target(self) -> str:
        return str(self.data.get("target", DEFAULTS["target"]))

    def port(self, proto: str) -> int:
        return int(self.data.get("ports", {}).get(proto, DEFAULTS["ports"][proto]))

    @property
    def connect_timeout(self) -> float:
        return float(self.data.get("connect_timeout", DEFAULTS["connect_timeout"]))

    @property
    def read_timeout(self) -> float:
        return float(self.data.get("read_timeout", DEFAULTS["read_timeout"]))

    @property
    def retries(self) -> int:
        return max(0, int(self.data.get("retries", DEFAULTS["retries"])))

    def get(self, key: str, default=None):
        return self.data.get(key, default)

    def set(self, key: str, value: str) -> None:
        """Set a dotted key (e.g. 'target' or 'ports.modbus'); values are coerced from strings."""
        node = self.data
        parts = key.split(".")
        for p in parts[:-1]:
            node = node.setdefault(p, {})
        node[parts[-1]] = _coerce(value)

    def as_dict(self) -> dict:
        return self.data


def _coerce(value: str):
    if isinstance(value, (int, float, bool)):
        return value
    low = value.lower()
    if low in ("true", "false"):
        return low == "true"
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return value
