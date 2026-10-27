# The `sysrupt` player toolkit

`sysrupt` is the player‑side command‑line toolkit for the range. It is a **dependency‑free Python 3.9+**
package (standard library only) that speaks the badge's industrial protocols and verifies challenge
progress. It is served by the badge and embedded in the firmware, so the downloadable copy is provably
identical to what ships.

> The authoritative, always‑current command reference is
> [`tools/sysrupt/COMMANDS.md`](../tools/sysrupt/COMMANDS.md), generated from the command registry.
> This guide covers layout, concepts and workflow; it does not restate every flag.

## Get it

Join the badge Wi‑Fi, then download from the badge:

```bash
curl -O http://10.0.1.1/toolkit.zip
unzip toolkit.zip && cd sysrupt-toolkit-1.0.0
./bin/sysrupt help            # grouped command reference (same content as COMMANDS.md)
./bin/sysrupt status          # probe the badge's challenge ports (read-only)
```

The source also lives in‑tree at [`tools/sysrupt/`](../tools/sysrupt/). See
[`tools/sysrupt/INSTALL.md`](../tools/sysrupt/INSTALL.md) and
[`tools/sysrupt/README.md`](../tools/sysrupt/README.md).

## Layout

```
tools/sysrupt/
  bin/sysrupt            entry-point wrapper
  sysrupt/
    __main__.py          CLI entry
    cli.py               argument parsing / dispatch
    registry.py          command registry — the single source of truth for every command
    context.py           per-run context (open sockets, config, cleanup)
    config.py            persisted configuration (target, ports, timeouts)
    transport.py         TCP/UDP transport helpers
    timing.py            opt-in timing / sequence engine
    result.py            structured result + verification-ladder reporting
    history.py           command history
    util.py              shared helpers
    <protocol modules>   modbus, iec104, tristation, goose
  tests/                 pytest suite (132 tests)
  COMMANDS.md            generated command reference
  make_release.py        build the deterministic release ZIP
  gen_embedded_zip.py    regenerate the firmware-embedded ToolkitZip.{h,cpp}
  gen_commands.py        regenerate COMMANDS.md from the registry
```

## Command groups

`registry.py` defines the commands; `sysrupt help <group>` lists each group's commands.

| Group | Covers |
|---|---|
| core | `help`, `status` (read‑only port probe), `version` |
| `config` | view/change saved configuration (target, ports, timeouts) |
| `modbus` | Modbus/TCP read/write workflows (incl. timing‑sensitive variants) |
| `iec104` | IEC‑104 interrogation and control (incl. wait‑until) |
| `tristation` | TriStation‑lite read/dump workflows (incl. watch/diff) |
| `goose` | GOOSE training: passive sniff, offline decode, explicit transmit, sustain |

Commands that transmit a control/write are marked **`[STATE-CHANGING]`** in `COMMANDS.md`. Everything
else is read‑only.

## Configuration and target addressing

Configuration is persisted and viewable:

```bash
./bin/sysrupt config show                 # effective configuration
./bin/sysrupt config set target 10.0.1.1  # the badge gateway
./bin/sysrupt config set ports.modbus 502
./bin/sysrupt config path                 # where the config file lives
```

Defaults target the badge (`10.0.1.1`) on the documented challenge ports.

## Verification ladder

Results are reported on a four‑level ladder so a player can tell *how far* an action got:

1. **request** — the toolkit sent the request,
2. **ack** — the target acknowledged it,
3. **observed state change** — the simulated plant moved to the expected state,
4. **challenge completion** — the badge recognizes the challenge as solved.

This makes partial progress legible (e.g. a write was accepted but the reconciliation loop reverted it).

## Timing, timeouts, rate and dry‑run

- An **opt‑in timing/sequence engine** (`timing.py`) supports the time‑sensitive challenges (short
  unlock windows, "hold for N seconds"). It is only engaged by commands that need it.
- Read/connect **timeouts** are configurable (`config set read_timeout …`). A timeout of `0` is honored
  as "no wait," not treated as "use the default."
- State‑changing commands that support a **`--dry-run`** build and print the frame/write without
  transmitting, so you can inspect exactly what would be sent.

## Read‑only vs. state‑changing

`status` and the various read/sniff/decode commands send nothing that changes plant state. Transmit /
write / sustain commands are explicitly marked `[STATE-CHANGING]` and print what they will send. Use
only against the authorized badge/lab (see the repository [`SECURITY.md`](../../../SECURITY.md)).

## Tests

```bash
cd Devices/vanguard/tools/sysrupt && python3 -m pytest -q    # 132 tests
```

## Rebuild the release ZIP and the embedded asset

```bash
cd Devices/vanguard/tools/sysrupt
python3 make_release.py                                  # -> release/sysrupt-toolkit-1.0.0.zip (deterministic)
python3 gen_embedded_zip.py release/sysrupt-toolkit-1.0.0.zip   # -> ../../source/ot/net/ToolkitZip.{h,cpp}
python3 gen_commands.py                                  # -> COMMANDS.md (from the registry)
```

The host test `ot_toolkitzip_tests` asserts the embedded bytes equal the release ZIP **byte‑for‑byte**
(verified SHA‑256 `4361fd88…77070a`). Always regenerate the embedded asset after changing the toolkit,
or that test will fail.
