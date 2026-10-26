# SPDX-License-Identifier: Apache-2.0
"""Small shared helpers used by more than one protocol module."""
from __future__ import annotations

import re

_FLAG_RE = re.compile(r"flag\{[^}]{1,60}\}")


def ascii_from_regs(regs) -> str:
    """Decode a list of 16-bit registers to ASCII (big-endian hi,lo per word), dropping NULs."""
    out = ""
    for w in regs or []:
        w &= 0xFFFF
        for c in (w >> 8, w & 0xFF):
            if c:
                out += chr(c)
    return out


def find_flag(text: str):
    """Return a flag{...} token if the DEVICE output contains one, else None.

    This only ever reports a flag that was actually read back from the target; the toolkit never
    contains a flag constant of its own.
    """
    if not text:
        return None
    m = _FLAG_RE.search(text)
    return m.group(0) if m else None


def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def parse_hex(s: str) -> bytes:
    """Parse a hex string that may use spaces, colons or a 0x prefix."""
    s = s.strip().replace("0x", "").replace("0X", "")
    s = re.sub(r"[\s:,]", "", s)
    if len(s) % 2 != 0:
        raise ValueError("hex string has an odd number of digits")
    return bytes.fromhex(s)
