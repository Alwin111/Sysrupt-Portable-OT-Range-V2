# SYSRUPT OT Toolkit (`sysrupt`)

A unified, **dependency-free** player-side command-line toolkit for the SYSRUPT OT Range V2 CTF
badge. One command (`sysrupt`) replaces the need to write separate Python scripts for timing-sensitive
or repetitive OT-protocol work.

It speaks two protocols against the badge (default target `10.0.1.1`):

| Module | Challenge | Transport |
|---|---|---|
| `modbus` | Reactor challenges — Stabilise (Ch07), Maintenance Override (Ch08) | Modbus/TCP, port 502 |
| `goose`  | Second Blackout (Ch06)                                            | GOOSE training stream, UDP 6180 |

The protocol byte formats are ported verbatim from the range's reference solver scripts
(`tools/solve/*.py`) so the toolkit stays faithful to the simulator.

## What it does — and does not — claim

Every operation reports a **four-level verification ladder**, because in an OT CTF each level looks
like the next but is not:

```
Request sent  ->  Protocol acknowledgement  ->  Observed state change  ->  Challenge completion
```

Only the last level is ever reported as success, and only when the **device's own output** shows it
(e.g. a flag block became readable). The toolkit contains **no flag constants, no answers, no
credentials, and no hidden solution sequences**. State-changing commands require you to supply the
target point, value and originator/key yourself.

## Quick start

```bash
./bin/sysrupt                      # interactive mode (prompt: sysrupt> )
./bin/sysrupt help                 # all commands, grouped
./bin/sysrupt help modbus          # Modbus commands, arguments, examples
./bin/sysrupt status               # bounded connectivity probe of the challenge ports
./bin/sysrupt target set 10.0.1.1  # set & persist the target

./bin/sysrupt modbus read-input 0 8
./bin/sysrupt iec104 interrogate
./bin/sysrupt goose sniff --seconds 5
```

Interactive and direct modes share the exact same command handlers.

## Optional timing / sequence engine

Timing is **opt-in** — ordinary commands never need it. A sequence is an explicit, ordered list of
steps in a JSON file; the engine runs them once each, in order, over one persistent connection per
protocol (so a Modbus unlock→write race lands inside the window), with per-step delays and deadlines
measured on a monotonic clock.

```bash
./bin/sysrupt sequence validate examples/recon_modbus.seq.json
./bin/sysrupt sequence run      examples/recon_modbus.seq.json
```

The engine executes **only** the steps you supply; it never infers a register, value, key or flag,
and never silently retries a state-changing step. The shipped example is **read-only** — adapt it
with your own findings. A timing-sensitive write sequence looks like:

```json
{
  "name": "my-timed-writes",
  "description": "TEMPLATE -- replace with your own addresses/values discovered during recon.",
  "steps": [
    { "command": "modbus write <ADDR> <VALUE>" },
    { "command": "modbus write <ADDR> <VALUE>", "deadline_ms": 2000 },
    { "command": "modbus verify <ADDR> <VALUE> --input", "delay_before_ms": 200, "verify": "contains:<VALUE>" }
  ]
}
```

## Operation history

```bash
./bin/sysrupt history            # recent operations
./bin/sysrupt history show 3     # one record in full
```

History stores timestamp, target, protocol, operation, duration, a short response summary and the
verification level. It records *whether* a flag was recovered, never the flag value, and never
credentials or secrets.

## Configuration

Config is a small JSON file (`~/.config/sysrupt/config.json` by default; override with
`$SYSRUPT_CONFIG` or a local `sysrupt.json`). You never edit Python to change the target.

```bash
./bin/sysrupt config show
./bin/sysrupt config set target 10.0.1.1
./bin/sysrupt config set read_timeout 8
```

See `sysrupt.example.json` for the full set of keys.

## Getting it from the badge

The badge serves this archive at `http://10.0.1.1/toolkit.zip` behind the engineer login (and links
it from the intranet HOME page). See `INSTALL.md` for the authenticated download + verification steps.

## Platform support

Tested on **Linux** with Python 3.9+. Windows/macOS are **not** claimed or tested. GOOSE inspection
here is an ordinary UDP receive (the simulator uses an application-level UDP training frame, not
raw-ethernet IEC 61850), so it needs **no special privileges or capture libraries**.

## Safety

- Read-only commands never transmit a control/write.
- The toolkit uses one connection at a time, bounded retries (never on a state-changing write), and
  no aggressive scanning — appropriate for the badge's small network stack.
- Use it **only** against the authorized CTF simulator, never real industrial equipment.

See `COMMANDS.md` for the full command reference and `INSTALL.md` for installation.
