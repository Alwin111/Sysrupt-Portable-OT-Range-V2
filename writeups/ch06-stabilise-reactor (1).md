# Challenge 6 — Stabilise Reactor

**Protocol:** Modbus/TCP · **Port:** 502/tcp · **Toolkit module:** `modbus`

## The Story

The reactor's control loop is out of its safe band. You need to identify which Modbus registers
correspond to the control inputs (likely something like a control-rod position and a cooling
input) and which represent the live sensor readings (flux, temperature, pressure), then drive the
inputs until the sensors sit inside the safe band — and keep them there long enough for the
range to register it as stable, not just a momentary pass-through.

## What You Know

- Target is `10.0.1.1:502`.
- `modbus read-input <start> <count>` reads read-only sensor-style registers; check
  `./bin/sysrupt help modbus` for the full command set in this build (write/holding register
  commands may be named differently than in other builds).
- There's no built-in "hold until stable" command in this build — sustaining a safe state across
  multiple writes needs either a manual loop or a `sequence` file, same as Maintenance Override.

---

## Step 1: Scan for live registers

<details>
<summary>Hint</summary>

Don't assume which addresses are sensors vs. actuators — read a block and see what values look
plausible as flux/temperature/pressure vs. small integer actuator positions.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt target set 10.0.1.1
./bin/sysrupt status
./bin/sysrupt help modbus
./bin/sysrupt modbus read-input 0 16
```
</details>

## Step 2: Identify the safe-band indicator

<details>
<summary>Hint</summary>

Look for a register that behaves like a bitmask or status flag rather than a continuous reading
— that's often how "all sensors in band" is communicated back to you, separate from the raw
sensor values themselves.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt modbus read-input <CANDIDATE_STATUS_REG> 1
```

Watch it while you experiment with writes in Step 3 — does it change in response?
</details>

## Step 3: Drive the actuators toward the band

<details>
<summary>Hint</summary>

Check the correct write command name for this build first — it may not be called `write`.
</details>

<details>
<summary>Show command</summary>

```bash
./bin/sysrupt modbus write <ACTUATOR_REG> <VALUE>
./bin/sysrupt modbus read-input <SENSOR_REG> 1
```

Adjust iteratively based on what the sensor reads back.
</details>

## Step 4: Hold the state, not just hit it once

<details>
<summary>Hint</summary>

A single write that happens to land in-band for one instant likely isn't enough — the range
probably wants the band held for some duration. Build a loop or sequence that re-applies the
value rather than writing once and walking away.
</details>

<details>
<summary>Show command</summary>

```bash
while true; do
  ./bin/sysrupt modbus write <ACTUATOR_REG> <VALUE>
  ./bin/sysrupt modbus read-input <STATUS_REG> 1
  sleep 1
done
```

Watch the status register; once it's confirmed healthy/flagged, stop the loop.
</details>

## What You Learned

- How to scan an unknown Modbus register map with `read-input` instead of assuming layout.
- Distinguishing a status/bitmask register from raw sensor values by behavior, not address
  guessing.
- Why a one-shot write may not satisfy a "hold in band" condition, and how to sustain a target
  state with a loop when the toolkit has no built-in hold command.
