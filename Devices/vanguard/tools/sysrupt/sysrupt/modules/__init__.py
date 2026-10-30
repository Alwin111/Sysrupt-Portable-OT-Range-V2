# SPDX-License-Identifier: Apache-2.0
"""Protocol modules. Each exposes register(reg) to add its commands to the shared registry."""
from . import goose, modbus

# iec104 and tristation modules removed: the Tripping Breaker and Safety Instrumented Bypass
# challenges were retired from the range, so their player-side protocols are no longer shipped.
MODULES = [modbus, goose]


def register_all(reg):
    for m in MODULES:
        m.register(reg)
