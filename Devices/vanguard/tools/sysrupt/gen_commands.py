#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerate COMMANDS.md from the command registry (the single source of truth).

The same registry drives help, dispatch AND this document, so there is never a second, drifting
list of commands. Run it after adding or changing a command:

    python3 gen_commands.py            # writes ./COMMANDS.md
    python3 gen_commands.py out.md     # writes a custom path

Dependency-free: standard library only.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sysrupt import __version__            # noqa: E402
from sysrupt.cli import build_registry     # noqa: E402


def _command_md(c) -> list:
    lines = [f"### `{c.usage()}`" + (" **[STATE-CHANGING]**" if c.modifies_state else ""),
             c.summary]
    bullets = [f"- `{a.display}` — {a.help}" for a in c.args if a.help]
    bullets += [f"- example: `sysrupt {ex}`" for ex in c.examples]
    if bullets:
        lines.append("")
        lines.extend(bullets)
    if c.long_help:
        lines.append("")
        lines.append(f"> {c.long_help}")
    lines.append("")
    return lines


def render(reg) -> str:
    lines = [
        f"# SYSRUPT Toolkit — Command Reference (v{__version__})",
        "",
        "Generated from the command registry (the single source of truth). "
        "`[STATE-CHANGING]` marks commands that transmit a control/write to the target.",
        "",
        "## Core commands",
        "",
    ]
    for name in sorted(reg.top):
        lines.extend(_command_md(reg.top[name]))
    for gname in sorted(reg.groups):
        g = reg.groups[gname]
        lines.append(f"## `{gname}` — {g.summary}")
        lines.append("")
        for cname in sorted(g.commands):
            lines.extend(_command_md(g.commands[cname]))
    return "\n".join(lines).rstrip() + "\n"


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    out_path = argv[0] if argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), "COMMANDS.md")
    text = render(build_registry())
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {out_path} ({text.count(chr(10))} lines, {len(text)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
