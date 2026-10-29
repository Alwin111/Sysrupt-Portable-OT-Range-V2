# `source/ot/` — the network-facing range

Everything a player can talk to over the badge network lives here: the simulated plants, the protocol
servers that expose them, and the `:80` web server.

## Layout

| Path | What it is |
|------|------------|
| `sim/` | **Simulated plants** — pure logic, no hardware or sockets, fully host-testable. See [`sim/README.md`](sim/README.md). |
| `net/` | **Protocol servers + web** — the listeners and the `:80` HTTP server (HMI, intranet, historian) + embedded toolkit. See [`net/README.md`](net/README.md). |
| `*.cpp` / `*.h` | **Service wrappers** (`Iec104Service`, `SisService`, `GooseService`, `SnmpService`, `PumpService`, `OpcUaService`, …) — each owns a listener task and bridges one `sim` plant to the network. |
| `ui/` | OT-specific UI helpers used by the LCD challenge views. |

## How it fits together

`startAllServices()` brings every service up once at boot. A service wraps a `sim` plant behind a
protocol listener; the player drives the protocol, the plant's state changes, and the challenge's
completion predicate (in [`../ui/Challenges.h`](../ui/Challenges.h)) reads that state over the network.

## Testing

The plants and protocol logic are covered by host-side C++ tests (no hardware):

```bash
cd Devices/vanguard/test && ./run_all.sh
```

See the project [architecture doc](../../docs/ARCHITECTURE.md) for the full picture.
