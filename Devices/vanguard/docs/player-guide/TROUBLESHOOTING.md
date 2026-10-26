# Troubleshooting — SYSRUPT OT Range V2 (live)

Real issues hit during the live playthrough and how to resolve them.

## Connectivity

**Every port shows `filtered` / nothing answers.**
The badge's IP stack is memory-constrained (ESP32-S3, no PSRAM; free heap dips toward ~8 KB under load). Aggressive or parallel scans overwhelm LWIP and it silently drops probes — that is *not* "closed".
- Scan one port at a time: `nmap -Pn -sT -T2 --max-parallelism 1 -p <port> 10.0.1.1`.
- Prefer the protocol clients / `curl` over port sweeps.

**Badge totally unreachable: no ping, `ip neigh 10.0.1.1` is `INCOMPLETE`, serial silent.**
The IP stack has hung (common after heavy testing or long uptime). The WiFi radio still beacons (you stay associated) but TCP/IP is dead. Reboot the badge — a reboot is **not** a factory reset and the session/score persist in NVS (verified: the completion mask reloads unchanged). Reboot by power-cycling, or via a serial RTS pulse on `/dev/ttyACM0`.

**WiFi keeps dropping / I lose internet when I join the badge.**
If your machine has a wired connection, keep it: the badge (WiFi) and the internet (ethernet) coexist, since the badge subnet `10.0.1.0/24` routes over WiFi and the default route stays on ethernet. Don't modify routing tables or NM profiles.

**`nmcli` re-association is slow/flaky.**
Retry in a short loop; the badge AP occasionally needs a couple of attempts to hand out a DHCP lease.

## Per-challenge

**#02 program download rejected (status 2 "bad firmware").**
The `Debug_Key` (reg42) rolls every few seconds. Re-open the lock (reg1=1) and re-read reg42 **immediately** before the download; use that fresh key.

**#04 first solve fails (valve opens but no flag).**
There is **one shared pump device**. If you send a replay frame first (to see it rejected), that raises a 6-second alert lockout that eats into the 10-second hold. Do the fresh `SELECT`+`OPEN` first, or wait >6 s after any alert before opening, then hold the full 10 s.

**#06 reply says `stale`.**
The legitimate publisher's `stNum` advanced past your forged value. Re-sniff the current `stNum` and forge `stNum+1`.

**#09 keeps returning "hold it stable".**
Expected. The restore must persist ~10 s. Wait ~12 s and re-submit the same correct `seq`+`value` to retrieve the flag.

**#11 safe-band flickers off 7.**
The telemetry jitters; you must re-assert the setpoints each tick so all three conditions stay in band simultaneously. The included `ch11_reactor.py` loops and re-asserts.

**#12 `IR72` reads 0 right after opening the unlock.**
The unlock-expiry countdown is short and the read lags; this does not mean you missed the window. Judge success by `IR73` (last-authorised) adopting your value (100). Open `HR20`, then write `HR21=100` on the very next request.

## Verification

**A command "succeeded" but is the challenge really done?**
A protocol success code is not completion. Confirm the intended *state transition* (breaker open, trip disabled, health=1, safe-band=7, IR73 adopted, etc.) and, where the badge tracks it, that the completion persisted. The badge's NVS completion mask (`0x1FDE` for all 11) is the ground truth; it is read from the boot log.
