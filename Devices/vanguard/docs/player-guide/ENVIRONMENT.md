# Environment & Connectivity — SYSRUPT OT Range V2

## The badge
The Vanguard badge is an **ESP32-S3** running Tactility OS. It creates its own **WiFi access point** and *is* the range: one IP, `10.0.1.1`, hosts all services.

- SSID: `SYSRUPT-OT-V2` (the badge is the AP / gateway at `10.0.1.1`).
- No PSRAM → limited free internal heap (≈15–35 KB idle, dipping toward ~8 KB under load). **Be gentle:** one connection at a time, low rate. An aggressive `nmap` sweep makes every port read *filtered* because the tiny LWIP stack drops parallel probes — that is not "closed".
- Serial console on USB (`/dev/ttyACM0`, 115200) shows boot + service logs.

## This machine's connectivity (recorded at start)
Two interfaces were up simultaneously, so **the badge and the internet coexist** — no disconnect was needed:

| Interface | Role | Address |
|-----------|------|---------|
| `enxfab89447c31f` (wired) | internet | 10.197.220.111/24 — internet reachable |
| `wlo1` (WiFi) | badge | 10.0.1.2/24 — joined `SYSRUPT-OT-V2` |

Route to the badge: `10.0.1.1 dev wlo1 src 10.0.1.2`. Internet traffic goes out ethernet. No routing tables, NM profiles, or firewall rules were modified.

**One recovery action (approved):** at start the badge's IP stack was hung (ARP `INCOMPLETE`, no ping) from earlier heavy testing; it was rebooted via a serial RTS pulse. A reboot is **not** a factory reset and does **not** erase NVS — the completion/score state persisted (verified: the completion mask reloaded unchanged on boot).

## Live services (from the boot log)
```
OtHttp: Engineering HMI up on http://10.0.1.1/        (:80)
OtHttp: Defensive intranet up on http://10.0.1.1/intranet
Iec104: :2404   SIS: :1502   Pump: :7700   OpcUa: :9005
Goose: :6180 (UDP)   Snmp: :161 (UDP)   OtTcp Listening: :502 :1502 :2404 :9005 :7700
```

## Tools used
- `curl` — web challenges and recon (`robots.txt`, `/control`, `/intranet`, `/hist`, `/evidence`, `/submit`).
- `python3` (stdlib only) — the protocol clients in `tools/solve/` (IEC-104, TriStation, OPC-UA-lite, custom pump, GOOSE/UDP, SNMP, Modbus/TCP). No external packages installed.
- `ping`, `nmap` (targeted, low-rate) — reachability + service checks.
- Headless Chromium — screenshots of the live badge web pages.
- Serial (pyserial) — read the badge boot log / completion mask (read-only, plus the one approved reboot).

## Scripts (`tools/solve/`)
`ch01_iec104.py`, `ch02_sis.py`, `ch03_opc.py`, `ch04_pump.py`, `ch06_goose.py`, `ch08_snmp.py`, `ch11_reactor.py`, `ch12_maint.py`, and `mbtcp.py` (minimal Modbus/TCP client). Each takes the badge IP as `argv[1]` (default `10.0.1.1`). Pure Python 3 — run `python3 chNN_*.py` while joined to the badge AP.

## Safety
This is a **simulated** training range. No command here touches real industrial hardware. State-changing actions (tripping the breaker, opening the pump valve, writing the reactor rod, etc.) only mutate the badge's in-RAM simulation and are required by the intended solutions.
