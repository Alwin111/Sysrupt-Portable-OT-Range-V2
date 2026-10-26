# SPDX-License-Identifier: Apache-2.0
"""Protocol modules. Each exposes register(reg) to add its commands to the shared registry."""
from . import goose, iec104, modbus, tristation

MODULES = [modbus, iec104, tristation, goose]


def register_all(reg):
    for m in MODULES:
        m.register(reg)
