---
layout: default
title: SYSRUPT Portable OT Range V2
---

# SYSRUPT Portable OT Range V2

**A hands-on OT/ICS cybersecurity training range that runs entirely on one ESP32-S3 badge.**

Join the badge's Wi-Fi, speak the plant's industrial protocols, and solve 8 progressive CTF challenges.
No cloud, no internet — the whole range is the badge.

---

## Getting Started

1. Flash the badge (see [INSTALL.md](https://github.com/Alwin111/Sysrupt-Portable-OT-Range-V2/blob/main/INSTALL.md)).
2. Join its open Wi-Fi: **`SYSRUPT-OT-V2-…`**. Your laptop gets `10.0.1.x`; the badge is `10.0.1.1`.
3. Open the operator HMI and download the toolkit:

```bash
firefox http://10.0.1.1/
curl -O http://10.0.1.1/toolkit.zip
unzip toolkit.zip && cd sysrupt-toolkit-1.0.0
./bin/sysrupt status          # read-only probe of the challenge ports
```

> **Be gentle:** the ESP32-S3 has no PSRAM and a tiny network stack. One connection at a time, low
> rate. Aggressive scans make ports read *filtered*, not "closed".

---

## Challenges

8 challenges · **1800 total points** · scoreboard on the badge LCD updates automatically.
Numbered defensive-first: **01–04 Defensive, 05–08 Offensive**.

> **Start here:** [Initial Access — Hacking into the IT Server](challenges/initial-access.md) — get the
> IT foothold (robots.txt → `/control` → login) before pivoting to the OT challenges below.

| # | Challenge | Type | Points | Guide |
|---|-----------|------|--------|-------|
| 01 | Historian SQL Injection | Defensive | 250 | [Start here](challenges/ch01.md) |
| 02 | Field Device Recovery | Defensive | 250 | [Hints](challenges/ch02.md) |
| 03 | Trojan Historian | Defensive | 150 | [Hints](challenges/ch03.md) |
| 04 | Device Takeover | Defensive | 100 | [Hints](challenges/ch04.md) |
| 05 | OPC Scout | Offensive | 150 | [Hints](challenges/ch05.md) |
| 06 | Second Blackout | Offensive | 300 | [Hints](challenges/ch06.md) |
| 07 | Stabilise the Reactor | Offensive | 300 | [Hints](challenges/ch07.md) |
| 08 | Maintenance Override | Offensive | 300 | [Hints](challenges/ch08.md) |

The guides are **spoiler-free** — they explain the protocol, the surface and the idea, not the answer.

---

## Network Map

The badge is the whole network: a Wi-Fi AP at `10.0.1.1` that hosts every service. Full detail:
[internal/NETWORK_MAP.md](internal/NETWORK_MAP.md).

| Surface | Port | Challenge |
|---------|------|-----------|
| Web (HMI / intranet / webmail / historian / toolkit) | TCP 80 | #01, #03, #04, #07, #08 entry |
| Modbus/TCP | TCP 502 | #07, #08 |
| OPC-UA-lite | TCP 9005 | #05 |
| GOOSE (training) | UDP 6180 | #06 |
| SNMP | UDP 161 | #02 |

The historian is served under `/hist` on TCP 80 (its own login + Filter-by-Source SQLi console), not a
separate port; it backs #01.

---

## The `sysrupt` Toolkit

A dependency-free Python 3.9+ CLI, served by the badge, that speaks the challenge protocols:

```bash
./bin/sysrupt help            # all command groups
./bin/sysrupt help modbus     # one protocol's commands
./bin/sysrupt config set target 10.0.1.1
```

| Group | For |
|-------|-----|
| `modbus` | Reactor challenges (#07, #08) |
| `goose` | Second Blackout (#06) |

Every result is reported on a four-level ladder — **request → ack → observed state change → challenge
completion** — so you can see exactly how far an action got. Full reference:
[`COMMANDS.md`](https://github.com/Alwin111/Sysrupt-Portable-OT-Range-V2/blob/main/Devices/vanguard/tools/sysrupt/COMMANDS.md).

---

## Tips

- Industrial protocols live on non-standard ports — scan specifically, don't rely on nmap's top-1000.
- Not everything is TCP. GOOSE and SNMP are UDP.
- Read the in-scenario email and intranet — the story tells you where each service is and what it does.
- Watch the HMI. Your attacks have visible impact on the process mimic and gauges.
- The badge LCD shows per-challenge objectives and two optional hint tiers (they cost points).

---

<p align="center"><strong>Flash it. Join the Wi-Fi. Hack the plant.</strong></p>
<p align="center">
  <a href="https://github.com/Alwin111/Sysrupt-Portable-OT-Range-V2">GitHub</a>
</p>
