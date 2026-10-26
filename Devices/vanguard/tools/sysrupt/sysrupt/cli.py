# SPDX-License-Identifier: Apache-2.0
"""The unified CLI: command registration, dispatch, help generation and interactive mode.

Direct execution (`sysrupt modbus read-holding 20 2`) and the interactive prompt (`sysrupt>`) go
through the SAME dispatch path and the SAME registered handlers -- there is no second implementation.
"""
from __future__ import annotations

import shlex
import sys

from . import PROGRAM, __version__
from .config import Config
from .context import Context
from .history import History
from .registry import ArgSpec, Command, CommandError, Registry
from .result import OperationResult
from .transport import TransportError, tcp_probe
from .timing import SequenceError, load_sequence, run_sequence
from .modules import register_all


# --------------------------------------------------------------------------- core command handlers

def _h_version(ctx, ns):
    return f"{PROGRAM} {__version__}"


def _h_status(ctx, ns):
    lines = [f"Target: {ctx.target}", ""]
    protos = [("web", "web"), ("modbus", "modbus"), ("iec104", "iec104"),
              ("tristation", "tristation"), ("goose", "goose"), ("historian", "historian")]
    for label, proto in protos:
        port = ctx.port(proto)
        if proto == "goose":
            lines.append(f"  {label:<11} udp/{port:<5}  (UDP; passive sniff -- not probed)")
            continue
        ok, ms = tcp_probe(ctx.target, port, timeout=min(ctx.cfg.connect_timeout, 2.0))
        state = f"reachable ({ms:.0f} ms)" if ok else "no response"
        lines.append(f"  {label:<11} tcp/{port:<5}  {state}")
    return "\n".join(lines)


def _h_target_show(ctx, ns):
    ports = ", ".join(f"{k}={v}" for k, v in ctx.cfg.data.get("ports", {}).items())
    return f"Target: {ctx.target}\nPorts:  {ports}"


def _h_target_set(ctx, ns):
    ctx.set_target(ns["ip"])
    ctx.cfg.set("target", ns["ip"])
    path = ctx.cfg.save()
    return f"Target set to {ns['ip']} (saved to {path})"


def _h_config_show(ctx, ns):
    import json
    return json.dumps(ctx.cfg.as_dict(), indent=2)


def _h_config_set(ctx, ns):
    ctx.cfg.set(ns["key"], ns["value"])
    path = ctx.cfg.save()
    return f"set {ns['key']} = {ctx.cfg.get(ns['key'].split('.')[0])!r} (saved to {path})"


def _h_config_path(ctx, ns):
    return ctx.cfg.path


def _h_history_list(ctx, ns):
    rows = ctx.history.recent(limit=ns.get("limit") or 20)
    if not rows:
        return "(no history yet)"
    out = ["  id  time                 target         protocol    operation           verify"]
    for idx, r in rows:
        out.append(f"  {idx:<3} {r.get('ts',''):<20} {r.get('target',''):<14} "
                   f"{r.get('protocol',''):<11} {r.get('operation',''):<19} {r.get('verify_label','')}")
    return "\n".join(out)


def _h_history_show(ctx, ns):
    import json
    rec = ctx.history.show(ns["id"])
    if rec is None:
        raise CommandError(f"no history record #{ns['id']}")
    return json.dumps(rec, indent=2)


def _h_sequence_validate(ctx, ns):
    name, desc, steps, _ = load_sequence(ns["file"])
    out = [f"Sequence '{name}' -- {desc or '(no description)'}", f"{len(steps)} step(s):"]
    for i, s in enumerate(steps, 1):
        tags = []
        if s.delay_before_ms:
            tags.append(f"wait {s.delay_before_ms}ms before")
        if s.deadline_ms:
            tags.append(f"deadline {s.deadline_ms}ms")
        if s.verify:
            tags.append(f"verify {s.verify}")
        out.append(f"  [{i}] {s.command}" + (f"   ({'; '.join(tags)})" if tags else ""))
    out.append("(validation only -- nothing executed)")
    return "\n".join(out)


def _h_sequence_run(ctx, ns):
    name, desc, steps, _ = load_sequence(ns["file"])
    if ns.get("no_timing"):
        for s in steps:
            s.delay_before_ms = 0
            s.delay_after_ms = 0
    ctx.keepalive = True
    cancelled = {"flag": False}

    def on_step(n, sr):
        tag = "OK" if sr.ok else "FAIL"
        print(f"  [{n}] {tag}  {sr.step.label}  ({sr.elapsed_ms:.1f} ms)")

    try:
        report = run_sequence(name, steps, executor=lambda toks: _invoke(ctx, toks),
                              on_step=on_step, cancel=lambda: cancelled["flag"])
    except KeyboardInterrupt:
        cancelled["flag"] = True
        report = None
    finally:
        ctx.keepalive = False
        ctx.close_all()
    if report is None:
        return "sequence cancelled"
    return report.format()


# --------------------------------------------------------------------------- help generation

def _h_help(ctx, ns):
    reg: Registry = ctx.registry
    topic = ns.get("topic")
    if not topic:
        return _help_overview(reg)
    # group?
    if topic in reg.groups:
        return _help_group(reg, topic)
    # top command?
    if topic in reg.top:
        return _help_command(reg.top[topic])
    # qualified "group command"?
    raise CommandError(f"no help topic '{topic}' (try: sysrupt help)")


def _help_overview(reg: Registry) -> str:
    out = [f"{PROGRAM} {__version__} -- SYSRUPT OT Range player toolkit", "",
           "Usage: sysrupt [--target IP] [--verbose] [COMMAND ...]",
           "       sysrupt                 (no command -> interactive mode)", "",
           "Core commands:"]
    for name in sorted(reg.top):
        out.append(f"  {name:<22} {reg.top[name].summary}")
    out.append("")
    out.append("Command groups (use 'help <group>' for detail):")
    for gname in sorted(reg.groups):
        g = reg.groups[gname]
        subs = ", ".join(sorted(g.commands))
        out.append(f"  {gname:<12} {g.summary}")
        out.append(f"               {subs}")
    out.append("")
    out.append("State-changing commands are marked in their group help. A protocol acknowledgement")
    out.append("is never reported as a solved challenge -- only an observed completion signal is.")
    return "\n".join(out)


def _help_group(reg: Registry, gname: str) -> str:
    g = reg.groups[gname]
    out = [f"{gname} -- {g.summary}", ""]
    for cname in sorted(g.commands):
        c = g.commands[cname]
        flag = "  [STATE-CHANGING]" if c.modifies_state else ""
        avail = "" if c.available else "  [UNAVAILABLE / planned]"
        out.append(f"  {c.usage()}{flag}{avail}")
        out.append(f"      {c.summary}")
        for a in c.args:
            out.append(f"        {a.display:<22} {a.help}")
        for ex in c.examples:
            out.append(f"      e.g.  sysrupt {ex}")
        if c.long_help:
            out.append(f"      note: {c.long_help}")
        out.append("")
    return "\n".join(out).rstrip()


def _help_command(c: Command) -> str:
    out = [f"  {c.usage()}", f"      {c.summary}"]
    if c.modifies_state:
        out.append("      [STATE-CHANGING]")
    for a in c.args:
        out.append(f"        {a.display:<22} {a.help}")
    for ex in c.examples:
        out.append(f"      e.g.  sysrupt {ex}")
    if c.long_help:
        out.append(f"      note: {c.long_help}")
    return "\n".join(out)


# --------------------------------------------------------------------------- registration

def register_core(reg: Registry) -> None:
    reg.add(Command("help", "Show help; 'help <group>' for a protocol's commands", _h_help,
                    args=[ArgSpec("topic", "a command group or core command", required=False)],
                    examples=["help", "help modbus"]))
    reg.add(Command("version", "Show the toolkit version", _h_version, examples=["version"]))
    reg.add(Command("status", "Probe the target's challenge ports (bounded, read-only)", _h_status,
                    examples=["status"]))

    reg.add_group("target", "View or set the badge target address")
    reg.add(Command("show", "Show the current target and ports", _h_target_show, group="target"))
    reg.add(Command("set", "Set and persist the target IP", _h_target_set, group="target",
                    args=[ArgSpec("ip", "target IP address")], examples=["target set 10.0.1.1"]))
    reg.group_defaults["target"] = "show"

    reg.add_group("config", "View or change saved configuration")
    reg.add(Command("show", "Print the effective configuration", _h_config_show, group="config"))
    reg.add(Command("set", "Set a config key (dotted) and save", _h_config_set, group="config",
                    args=[ArgSpec("key", "e.g. target or ports.modbus"), ArgSpec("value", "value")],
                    examples=["config set read_timeout 8", "config set ports.modbus 502"]))
    reg.add(Command("path", "Show the config file path", _h_config_path, group="config"))
    reg.group_defaults["config"] = "show"

    reg.add_group("history", "Operation history")
    reg.add(Command("list", "List recent operations", _h_history_list, group="history",
                    args=[ArgSpec("--limit", "how many to show", required=False, type="int", default=20)]))
    reg.add(Command("show", "Show one history record in full", _h_history_show, group="history",
                    args=[ArgSpec("id", "record id", type="int")], examples=["history show 3"]))
    reg.group_defaults["history"] = "list"

    reg.add_group("sequence", "Optional timing/sequence engine (explicit steps only)")
    reg.add(Command("validate", "Parse and print a sequence file without running it",
                    _h_sequence_validate, group="sequence",
                    args=[ArgSpec("file", "sequence JSON file")], examples=["sequence validate seq.json"]))
    reg.add(Command("run", "Run a sequence file (persistent connection, per-step timing)",
                    _h_sequence_run, group="sequence", modifies_state=True,
                    args=[ArgSpec("file", "sequence JSON file"),
                          ArgSpec("--no-timing", "ignore the file's delays (run as fast as possible)",
                                  required=False, is_flag=True, default=False)],
                    examples=["sequence run maint_override.seq.json"],
                    long_help="Runs ONLY the steps in the file, once each, in order. It never infers "
                              "values or retries a state-changing step."))


def build_registry() -> Registry:
    reg = Registry()
    register_core(reg)
    register_all(reg)
    return reg


# --------------------------------------------------------------------------- dispatch

def _invoke(ctx, tokens: list):
    """Resolve + parse + run one command, returning its result. No printing / history / cleanup."""
    cmd, rest = ctx.registry.resolve(tokens)
    ns = cmd.parse(rest)
    return cmd.handler(ctx, ns)


def dispatch(ctx, tokens: list) -> int:
    """Top-level: run one command, print its output, record history, clean up. Returns an exit code."""
    if not tokens:
        return 0
    try:
        res = _invoke(ctx, tokens)
        if isinstance(res, OperationResult):
            print(res.format(verbose=ctx.verbose))
            ctx.history.record(res)
            return 0 if res.verify.value > 0 else 1
        elif isinstance(res, str):
            print(res)
        return 0
    except CommandError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except (SequenceError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except TransportError as e:
        print(f"transport error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n(cancelled)", file=sys.stderr)
        return 130
    finally:
        if not ctx.keepalive:
            ctx.close_transient()


# --------------------------------------------------------------------------- interactive mode

def interactive(ctx) -> int:
    print(f"{PROGRAM} {__version__} -- interactive mode. Target {ctx.target}. "
          f"Type 'help' or 'exit'.")
    while True:
        try:
            line = input(f"{PROGRAM}> ")
        except EOFError:
            print()
            return 0
        except KeyboardInterrupt:
            print("\n(use 'exit' to quit)")
            continue
        line = line.strip()
        if not line:
            continue
        if line in ("exit", "quit"):
            return 0
        try:
            tokens = shlex.split(line)
        except ValueError as e:
            print(f"error: {e}", file=sys.stderr)
            continue
        dispatch(ctx, tokens)


# --------------------------------------------------------------------------- entry point

def _preparse(argv: list):
    """Pull leading global options; return (global_opts, remaining_tokens)."""
    opts = {"target": None, "verbose": False, "config": None}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("-t", "--target") and i + 1 < len(argv):
            opts["target"] = argv[i + 1]; i += 2
        elif a in ("-v", "--verbose"):
            opts["verbose"] = True; i += 1
        elif a == "--config" and i + 1 < len(argv):
            opts["config"] = argv[i + 1]; i += 2
        elif a in ("--version",):
            opts["_version"] = True; i += 1
        else:
            break
    return opts, argv[i:]


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    opts, rest = _preparse(argv)
    cfg = Config.load(opts["config"])
    reg = build_registry()
    ctx = Context(cfg, history=History(), verbose=opts["verbose"], target_override=opts["target"])
    ctx.registry = reg
    if opts.get("_version"):
        print(f"{PROGRAM} {__version__}")
        return 0
    try:
        if not rest:
            return interactive(ctx)
        return dispatch(ctx, rest)
    finally:
        ctx.close_all()
