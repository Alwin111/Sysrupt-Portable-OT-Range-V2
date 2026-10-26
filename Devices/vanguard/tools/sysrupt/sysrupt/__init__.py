# SPDX-License-Identifier: Apache-2.0
"""SYSRUPT OT Range player toolkit.

A unified, dependency-free command-line toolkit for interacting with the SYSRUPT OT Range
challenge badge (default target 10.0.1.1). It provides one CLI (`sysrupt`) over four OT
protocol modules (Modbus/TCP, IEC-104, TriStation-lite, GOOSE), a shared transport layer, an
optional timing/sequence engine, operation history, and an explicit request -> acknowledgement
-> observed-state -> challenge-completion verification model.

The protocol byte formats are ported verbatim from the repository's reference solver scripts in
``tools/solve`` so the toolkit stays faithful to the actual simulator; it never hardcodes flags,
answers, credentials, or hidden solution sequences.
"""

__version__ = "1.0.0"
PROGRAM = "sysrupt"
