# SPDX-License-Identifier: Apache-2.0
"""The command registry -- the single source of truth for parsing AND help.

Every command (core or protocol) is registered here once, with its arguments, examples and a
``modifies_state`` flag. The same registry drives direct CLI execution and the interactive prompt,
and generates the help text, so there is never a second, drifting list of commands.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional


class CommandError(Exception):
    """A user-facing error (bad arguments, unknown command). Printed without a traceback."""


def parse_int(s: str) -> int:
    """Accept decimal, 0x hex, or 0b binary."""
    s = s.strip()
    try:
        return int(s, 0) if s.lower().startswith(("0x", "0b", "0o")) else int(s)
    except ValueError as e:
        raise CommandError(f"expected an integer, got '{s}'") from e


@dataclass
class ArgSpec:
    name: str
    help: str = ""
    required: bool = True
    type: str = "str"           # str | int | hex (alias of int) | float
    default: object = None
    choices: Optional[list] = None
    is_flag: bool = False       # --name boolean switch
    metavar: str = ""

    @property
    def display(self) -> str:
        key = self.name.lstrip("-")
        mv = self.metavar or key.upper()
        if self.is_flag:
            return f"--{key}"
        if self.name.startswith("--"):
            return f"--{key} <{mv}>"
        return f"<{key}>" if self.required else f"[{key}]"

    def coerce(self, raw: str):
        if self.type in ("int", "hex"):
            v = parse_int(raw)
        elif self.type == "float":
            try:
                v = float(raw)
            except ValueError as e:
                raise CommandError(f"{self.name}: expected a number, got '{raw}'") from e
        else:
            v = raw
        if self.choices is not None and v not in self.choices:
            raise CommandError(f"{self.name}: must be one of {self.choices}, got '{raw}'")
        return v


@dataclass
class Command:
    name: str
    summary: str
    handler: Callable           # handler(ctx, ns) -> OperationResult | str | None
    group: str = ""             # "" for top-level core commands
    args: list = field(default_factory=list)
    examples: list = field(default_factory=list)
    modifies_state: bool = False
    available: bool = True      # False => shown as "unavailable (planned)"
    long_help: str = ""

    @property
    def qualified(self) -> str:
        return f"{self.group} {self.name}".strip()

    def usage(self) -> str:
        parts = [f"sysrupt {self.qualified}"]
        parts += [a.display for a in self.args]
        return " ".join(parts)

    def parse(self, tokens: list) -> dict:
        """Parse a token list into a namespace dict using this command's ArgSpecs."""
        if not self.available:
            raise CommandError(f"'{self.qualified}' is planned but not yet implemented")
        # Namespace keys are normalised to valid identifiers (dashes -> underscores) so a handler can
        # read `ns["max_iters"]` for a `--max-iters` option. The displayed name keeps its dashes.
        positional = [a for a in self.args if not a.name.startswith("--")]
        options = {a.name.lstrip("-").replace("-", "_"): a for a in self.args if a.name.startswith("--")}
        ns: dict = {}
        for a in self.args:
            key = a.name.lstrip("-").replace("-", "_")
            ns[key] = (True if a.is_flag and a.default is None else a.default) if a.is_flag else a.default
            if a.is_flag:
                ns[key] = bool(a.default)
        pos_i = 0
        i = 0
        while i < len(tokens):
            tok = tokens[i]
            if tok.startswith("--"):
                raw = tok[2:]
                key = raw.replace("-", "_")
                if key not in options:
                    raise CommandError(f"unknown option '--{raw}' for '{self.qualified}'")
                spec = options[key]
                if spec.is_flag:
                    ns[key] = True
                    i += 1
                else:
                    if i + 1 >= len(tokens):
                        raise CommandError(f"option '--{raw}' needs a value")
                    ns[key] = spec.coerce(tokens[i + 1])
                    i += 2
            else:
                if pos_i >= len(positional):
                    raise CommandError(f"unexpected argument '{tok}' for '{self.qualified}'")
                spec = positional[pos_i]
                ns[spec.name] = spec.coerce(tok)
                pos_i += 1
                i += 1
        # enforce required positionals
        for a in positional[pos_i:]:
            if a.required and ns.get(a.name) is None:
                raise CommandError(f"missing required argument <{a.name}>\n  usage: {self.usage()}")
        return ns


@dataclass
class Group:
    name: str
    summary: str
    commands: dict = field(default_factory=dict)


class Registry:
    def __init__(self):
        self.groups: dict = {}
        self.top: dict = {}           # group == "" core commands keyed by name
        self.group_defaults: dict = {}  # group name -> default subcommand when none is given

    def add_group(self, name: str, summary: str) -> None:
        self.groups.setdefault(name, Group(name, summary))

    def add(self, cmd: Command) -> None:
        if cmd.group:
            self.add_group(cmd.group, self.groups.get(cmd.group, Group(cmd.group, "")).summary)
            self.groups[cmd.group].commands[cmd.name] = cmd
        else:
            self.top[cmd.name] = cmd

    def command(self, group: str, name: str, **kw) -> Callable:
        """Decorator to register a handler as a Command."""
        def deco(fn):
            self.add(Command(name=name, group=group, handler=fn, **kw))
            return fn
        return deco

    def resolve(self, tokens: list):
        """Map leading tokens to (Command, remaining_tokens). Raises CommandError if unknown."""
        if not tokens:
            raise CommandError("no command given")
        head = tokens[0]
        if head in self.top:
            return self.top[head], tokens[1:]
        if head in self.groups:
            grp = self.groups[head]
            if len(tokens) < 2:
                default = self.group_defaults.get(head)
                if default:
                    return grp.commands[default], []
                raise CommandError(f"'{head}' needs a subcommand (try: sysrupt help {head})")
            sub = tokens[1]
            if sub not in grp.commands:
                raise CommandError(f"unknown '{head}' subcommand '{sub}' (try: sysrupt help {head})")
            return grp.commands[sub], tokens[2:]
        raise CommandError(f"unknown command '{head}' (try: sysrupt help)")

    def all_commands(self):
        for c in self.top.values():
            yield c
        for g in self.groups.values():
            for c in g.commands.values():
                yield c
