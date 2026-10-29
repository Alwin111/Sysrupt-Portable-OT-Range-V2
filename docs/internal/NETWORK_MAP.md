# Network Map

The badge is the network. It runs a Wi-Fi access point and routes/NATs for its clients, so one
address — **`10.0.1.1`** — hosts every service you'll interact with.

## Joining

| Setting | Value |
|---------|-------|
| SSID | `SYSRUPT-OT-V2-NNN` (open; `NNN` is from the badge MAC) |
| Badge / gateway | `10.0.1.1` |
| Your laptop | `10.0.1.x` (assigned automatically) |
| Serial console | `/dev/ttyACM0` @ 115200 (boot + service logs) |

The badge enables IPv4 forwarding + NAPT, so the scenario's "deeper zone" addresses
(`10.0.2.x`–`10.0.5.x`, used in the story) are reachable through the badge.

## Ports

| Port | Proto | Service | Challenge | `sysrupt` group |
|------|-------|---------|-----------|-----------------|
| 80 | TCP | Web: reactor HMI, intranet, IR webmail, historian console, `/toolkit.zip` | #07, #09, #10, #11/#12 entry | — (browser / `curl`) |
| 502 | TCP | Modbus/TCP — reactor control | #11, #12 | `modbus` |
| 2404 | TCP | IEC 60870-5-104 — substation breaker | #01 | `iec104` |
| 1502 | TCP | TriStation-lite — safety controller | #02 | `tristation` |
| 9005 | TCP | OPC-UA-lite — OPC server | #03 | — |
| 7700 | TCP | Pump radio gateway | #04 | — |
| 6180 | UDP | GOOSE (training) bay | #06 | `goose` |
| 161 | UDP | SNMP — field RTU | #08 | — |

## Web surfaces on :80

| Path | What it is |
|------|------------|
| `/` and `/hmi` | Operator reactor HMI with the live process mimic and gauges |
| `/intranet` | Corporate intranet landing (incident overview, GOOSE breaker status) |
| `/mail` | IR webmail — the scenario tasking and its evidence attachments (downloadable files) |
| `/inventory` | Engineering asset inventory + OUI registry |
| `/investigate` | "Record findings" submission forms (#07, #09, #10) |
| `/toolkit.zip` | The embedded `sysrupt` player toolkit |
| historian console | A **separate internal system**; its address is discoverable from the scenario (not in the main nav) |

## Scanning advice

The ESP32-S3 has no PSRAM and a small lwIP stack. **Scan gently:**

```bash
# good — slow, targeted, TCP connect scan:
nmap -sT -T2 -p 80,502,1502,2404,7700,9005 10.0.1.1

# UDP services, one at a time:
nmap -sU -p 161 10.0.1.1
nmap -sU -p 6180 10.0.1.1
```

> An aggressive parallel sweep (`-T4`/`-T5`, full port range) makes ports read **filtered** because the
> stack drops concurrent probes — that is not the same as "closed." One connection at a time is the rule.
