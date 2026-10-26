# SPDX-License-Identifier: Apache-2.0
"""The shared operation-result model and its four-level verification ladder.

The toolkit deliberately separates four distinct facts about any operation, because an OT CTF
is full of traps where one looks like the next:

    SENT           the request bytes left the socket
    ACKED          the device returned a positive protocol acknowledgement
    STATE_CHANGED  a follow-up read showed the target's observable state actually changed
    COMPLETED      the challenge's own completion signal was observed (e.g. a flag block became
                   readable) -- the only level that may be called success

A positive acknowledgement is NOT proof the physical point moved, and an observed state change is
NOT proof the challenge logic accepted it. Only COMPLETED, confirmed from the device's real
response, is reported as success. Nothing here hardcodes a flag or infers completion it did not see.
"""
from __future__ import annotations

import enum
from dataclasses import dataclass, field
from typing import Optional


class Verify(enum.IntEnum):
    ERROR = 0          # the request could not be sent / transport failed
    SENT = 1           # bytes transmitted, no (positive) response yet
    ACKED = 2          # positive protocol acknowledgement received
    STATE_CHANGED = 3  # a follow-up observation confirmed the state changed
    COMPLETED = 4      # the challenge completion signal was observed

    @property
    def label(self) -> str:
        return {
            Verify.ERROR: "Transport error",
            Verify.SENT: "Request sent (no positive response)",
            Verify.ACKED: "Protocol acknowledgement received",
            Verify.STATE_CHANGED: "Observed state change confirmed",
            Verify.COMPLETED: "Challenge completion confirmed",
        }[self]


@dataclass
class OperationResult:
    """A structured, uniformly-formatted result for every protocol operation."""
    protocol: str
    operation: str
    target: str
    verify: Verify = Verify.SENT
    request: str = ""           # human/hex description of what was sent
    response: str = ""          # human/hex description of what came back
    detail: str = ""            # extra notes (never a fabricated success)
    elapsed_ms: float = 0.0
    # Only ever set from a value actually read back from the device, never a constant in this file.
    flag: Optional[str] = None
    extra: dict = field(default_factory=dict)

    @property
    def success(self) -> bool:
        """Success means the challenge's own completion signal was observed -- nothing less."""
        return self.verify == Verify.COMPLETED

    def format(self, verbose: bool = False) -> str:
        lines = [
            f"Operation:            {self.protocol} {self.operation}",
            f"Target:               {self.target}",
            f"Request:              {self.request or 'Sent'}",
            f"Protocol response:    {self.response or '(none)'}",
            f"Verification:         {self.verify.label}",
            f"Elapsed:              {self.elapsed_ms:.1f} ms",
        ]
        if self.flag:
            lines.append(f"Flag (from device):   {self.flag}")
        if self.detail:
            lines.append(f"Note:                 {self.detail}")
        # `extra` holds the operation's own payload (register dumps, interrogation points, captured
        # frames, decoded values), so it is always shown -- not gated behind --verbose.
        for k, v in self.extra.items():
            lines.append(f"{k}:{'' if str(v).startswith(chr(10)) else ' '}{v}")
        return "\n".join(lines)
