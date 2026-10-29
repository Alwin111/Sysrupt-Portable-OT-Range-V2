# Operator Setup

This is the short operator runbook for running a session. The full build/flash guide is
[INSTALL.md](../../INSTALL.md).

## 1. Put firmware on the badge

**Pre-built:**

```bash
./flash_badge.sh -p /dev/ttyACM0 -y        # preserves saved progress (NVS)
```

**From source:**

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.1 \
  bash -c "pip install lark && idf.py build"
./flash_badge.sh -p /dev/ttyACM0 -y
```

## 2. Verify it's live

Watch the serial console (`/dev/ttyACM0`, 115200) for the boot log. You should see the Wi-Fi AP come up
and lines such as:

```
OtHttp: Engineering HMI up on http://10.0.1.1/
Iec104: :2404   SIS: :1502   Pump: :7700   OpcUa: :9005
Goose: :6180 (UDP)   Snmp: :161 (UDP)
```

From a laptop joined to `SYSRUPT-OT-V2-…`:

```bash
ping -c2 10.0.1.1
curl -s -o /dev/null -w '%{http_code}\n' http://10.0.1.1/
```

## 3. Hand out to players

Players join the open Wi-Fi, open `http://10.0.1.1/`, and download the toolkit from
`http://10.0.1.1/toolkit.zip`. Point them at [`docs/index.md`](../index.md) and the
[challenge guides](../challenges/).

## Credentials note

This is a deliberately vulnerable training range: in-scenario credentials and insecure defaults are
**intentional challenge fixtures** that players are meant to discover. They are **not** listed in this
repository (that would spoil the challenges). The badge AP is open by design so players can join quickly
in a lab; keep the badge on an isolated lab network, never bridged to anything real.

## Reset between sessions

- **Clear progress, keep firmware:** the **Reset Session** screen in the badge LCD menu.
- **Full wipe + re-flash:** `./flash_badge.sh -p /dev/ttyACM0 -y --erase`.

A plain reboot (or a serial RTS pulse) does **not** erase NVS — scores and completions persist.

## Scoring at a glance

- 11 challenges, **2500 points** total (7 offensive / 4 defensive).
- Two optional hint tiers per challenge cost 10 and 20 points when revealed.
- Completion is detected automatically over the network; the LCD shows a shared "Challenge Complete"
  screen and writes the score to NVS.
