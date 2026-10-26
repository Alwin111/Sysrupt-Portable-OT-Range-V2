# SPDX-License-Identifier: Apache-2.0
import os

from sysrupt.config import Config
from sysrupt.history import History
from sysrupt.result import OperationResult, Verify


def test_config_defaults_when_missing(tmp_path):
    cfg = Config.load(str(tmp_path / "none.json"))
    assert cfg.target == "10.0.1.1"
    assert cfg.port("modbus") == 502
    assert cfg.connect_timeout == 5.0


def test_config_set_save_reload(tmp_path):
    p = str(tmp_path / "c.json")
    cfg = Config.load(p)
    cfg.set("target", "10.0.1.2")
    cfg.set("ports.modbus", "5020")
    cfg.set("read_timeout", "8")
    cfg.save()
    again = Config.load(p)
    assert again.target == "10.0.1.2"
    assert again.port("modbus") == 5020
    assert again.read_timeout == 8.0


def test_config_coerces_bool():
    cfg = Config()
    cfg.set("verbose", "true")
    assert cfg.get("verbose") is True


def test_history_roundtrip_and_no_flag_leak(tmp_path):
    h = History(str(tmp_path / "h.jsonl"))
    res = OperationResult("Modbus", "write", "10.0.1.1:502", verify=Verify.COMPLETED,
                          flag="flag{should_not_be_stored}")
    h.record(res)
    rows = h.recent()
    assert len(rows) == 1
    idx, rec = rows[0]
    assert rec["protocol"] == "Modbus"
    assert rec["flag_recovered"] is True
    # the actual flag value must never be written to history
    raw = open(h.path, encoding="utf-8").read()
    assert "flag{should_not_be_stored}" not in raw


def test_history_show_out_of_range(tmp_path):
    h = History(str(tmp_path / "h.jsonl"))
    assert h.show(99) is None
