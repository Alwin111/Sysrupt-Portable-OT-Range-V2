# SYSRUPT Toolkit — Command Reference (v1.0.0)

Generated from the command registry (the single source of truth). `[STATE-CHANGING]` marks commands that transmit a control/write to the target.

## Core commands

### `sysrupt help [topic]`
Show help; 'help <group>' for a protocol's commands

- `[topic]` — a command group or core command
- example: `sysrupt help`
- example: `sysrupt help modbus`

### `sysrupt status`
Probe the target's challenge ports (bounded, read-only)

- example: `sysrupt status`

### `sysrupt version`
Show the toolkit version

- example: `sysrupt version`

## `config` — View or change saved configuration

### `sysrupt config path`
Show the config file path

### `sysrupt config set <key> <value>`
Set a config key (dotted) and save

- `<key>` — e.g. target or ports.modbus
- `<value>` — value
- example: `sysrupt config set read_timeout 8`
- example: `sysrupt config set ports.modbus 502`

### `sysrupt config show`
Print the effective configuration

## `goose` — GOOSE (Second Blackout, Ch06) -- passive sniff & explicit transmit

### `sysrupt goose capture <show> <file>`
Show a previously saved capture file

- `<show>` — literal 'show'
- `<file>` — capture file path
- example: `sysrupt goose capture show cap.json`

### `sysrupt goose decode <hex>`
Decode a single GOOSE frame from hex (offline)

- `<hex>` — 16-byte frame hex
- example: `sysrupt goose decode '47 53 01 01 0b b8 00 00 00 05 00 00 00 00 00 01'`

### `sysrupt goose send --appid <APPID> --stnum <STNUM> --confrev <CONFREV> --sqnum <SQNUM> --open --test --flags <FLAGS>` **[STATE-CHANGING]**
Construct and transmit a GOOSE frame -- STATE CHANGING

- `--appid <APPID>` — APPID (from your sniff)
- `--stnum <STNUM>` — stNum (you choose, e.g. last+1)
- `--confrev <CONFREV>` — confRev (from your sniff)
- `--sqnum <SQNUM>` — sqNum
- `--open` — set breaker-open flag (bit0)
- `--test` — set test flag (bit1)
- `--flags <FLAGS>` — raw flags byte (overrides --open/--test)
- example: `sysrupt goose send --appid 0x0bb8 --stnum 6 --confrev 1 --open`

> Builds the frame from the values you supply (nothing is inferred). The frame hex is printed before transmission. Sniff first to learn APPID, confRev and the current stNum.

### `sysrupt goose sniff --seconds <SECONDS> --save <SAVE>`
Passively capture GOOSE frames for N seconds (sends nothing)

- `--seconds <SECONDS>` — capture duration (1..30)
- `--save <SAVE>` — save captured frames to a JSON file
- example: `sysrupt goose sniff --seconds 5`
- example: `sysrupt goose sniff --seconds 5 --save cap.json`

### `sysrupt goose sustain --appid <APPID> --stnum <STNUM> --confrev <CONFREV> --duration <DURATION> --interval <INTERVAL> --open --test --flags <FLAGS> --dry-run` **[STATE-CHANGING]**
Hold a state: re-send one stNum (sqNum++) for a bounded time -- STATE CHANGING

- `--appid <APPID>` — APPID (from your sniff)
- `--stnum <STNUM>` — stNum to hold (the state change you are sustaining)
- `--confrev <CONFREV>` — confRev (from your sniff)
- `--duration <DURATION>` — seconds to sustain (1..30)
- `--interval <INTERVAL>` — seconds between frames (0.2..5)
- `--open` — set breaker-open flag (bit0)
- `--test` — set test flag (bit1)
- `--flags <FLAGS>` — raw flags byte (overrides --open/--test)
- `--dry-run` — preview the frames without transmitting
- example: `sysrupt goose sustain --appid 0x0bb8 --stnum 6 --confrev 1 --open --duration 5`

> Reuses the send encoder; unicast to the CONFIGURED target only (no broadcast, no interface selection, no scanning). sqNum progresses 0,1,2,... while stNum/confRev stay fixed. Duration and send-rate are capped. Transmitting is never success -- completion is claimed only if the device's reply carries the flag. Use --dry-run to preview first.

## `history` — Operation history

### `sysrupt history list --limit <LIMIT>`
List recent operations

- `--limit <LIMIT>` — how many to show

### `sysrupt history show <id>`
Show one history record in full

- `<id>` — record id
- example: `sysrupt history show 3`

## `iec104` — IEC-104 (Tripping Breaker, Ch01) -- interrogation & single command

### `sysrupt iec104 command <ioa> <value> --originator <ORIGINATOR>` **[STATE-CHANGING]**
Send a single command C_SC -- STATE CHANGING

- `<ioa>` — target IOA
- `<value>` — SCS value (0 or 1)
- `--originator <ORIGINATOR>` — originator address (from your recon)
- example: `sysrupt iec104 command 1001 0 --originator 7`

> Transmits one C_SC. You must supply the IOA, SCS value and originator yourself (derive them from the interrogation / evidence). The frame is printed before transmission. A positive ACTCON is only an ack.

### `sysrupt iec104 flag`
Re-interrogate and read the flag object block (if the RTU exposes it)

- example: `sysrupt iec104 flag`

### `sysrupt iec104 interrogate`
General interrogation; list all monitored points

- example: `sysrupt iec104 interrogate`

### `sysrupt iec104 read <ioa>`
Read one IOA's value from an interrogation

- `<ioa>` — information object address
- example: `sysrupt iec104 read 1001`

### `sysrupt iec104 status`
STARTDT handshake / protocol status

- example: `sysrupt iec104 status`

### `sysrupt iec104 wait-until <point> <value> --timeout <TIMEOUT> --interval <INTERVAL>`
Poll a point until it reaches a value, or time out (read-only)

- `<point>` — information object address (IOA) to watch
- `<value>` — the value to wait for
- `--timeout <TIMEOUT>` — overall timeout seconds (1..300)
- `--interval <INTERVAL>` — seconds between polls (0.2..10)
- example: `sysrupt iec104 wait-until 1001 0`
- example: `sysrupt iec104 wait-until 1001 0 --timeout 20 --interval 0.5`

> Reuses interrogation only -- it never sends a control command. Reports an OBSERVED state change when the value is seen (distinct from a solved challenge); confirm completion with 'iec104 flag'.

## `modbus` — Modbus/TCP (Maintenance Override, Ch12) -- registers & writes

### `sysrupt modbus decode <hex>`
Decode a register hex blob to uint16/int16/ascii (offline)

- `<hex>` — hex bytes, e.g. '01 90 00 64'
- example: `sysrupt modbus decode '01 90 00 64'`

### `sysrupt modbus dump <start> <count> --input`
Read and pretty-print a range of registers

- `<start>` — start address
- `<count>` — count (1..64)
- `--input` — input registers instead of holding
- example: `sysrupt modbus dump 0 8`
- example: `sysrupt modbus dump 48 16 --input`

### `sysrupt modbus hold <adjust> --until <UNTIL> --until-input --timeout <TIMEOUT> --interval <INTERVAL> --max-iters <N>` **[STATE-CHANGING]**
Apply an adjustment and hold until a condition -- STATE CHANGING

- `<adjust>` — the adjustment to apply each iteration, as <reg>=<value>
- `--until <UNTIL>` — stop when this condition holds, as <reg>=<value>
- `--until-input` — read the condition register as an input register (FC04)
- `--timeout <TIMEOUT>` — overall timeout seconds (1..300)
- `--interval <INTERVAL>` — seconds between iterations (0.2..10)
- `--max-iters <N>` — max iterations (1..200)
- example: `sysrupt modbus hold 73=100 --until 74=1 --timeout 12 --interval 0.5`

> A SAFE FRAMEWORK, not a controller: it writes ONLY the <reg>=<value> you give, reads state before each write, and stops as soon as your explicit --until condition is observed (or on timeout / max-iters / error). It never invents a register, value or control formula. Writes are allowed only against a loopback/private lab target. An observed condition is NOT challenge completion.

### `sysrupt modbus monitor <addr> --input --samples <SAMPLES> --interval <INTERVAL>`
Poll one register a bounded number of times

- `<addr>` — address
- `--input` — input register
- `--samples <SAMPLES>` — number of samples (1..30)
- `--interval <INTERVAL>` — seconds between samples
- example: `sysrupt modbus monitor 72 --input --samples 10 --interval 0.5`

### `sysrupt modbus race <unlock> <target> --window <WINDOW>` **[STATE-CHANGING]**
Time-boxed unlock->write over one connection -- STATE CHANGING

- `<unlock>` — the unlock write as <reg>=<value>
- `<target>` — the target write as <reg>=<value>
- `--window <WINDOW>` — max gap in ms (1..60000)
- example: `sysrupt modbus race 20=1 21=100 --window 500`

> Runs exactly two writes (unlock then target) in order over ONE persistent connection via the existing sequence engine -- no second timing engine, and the state-changing writes are never auto-retried. You supply both registers, both values and the window; nothing is inferred. The target is read back to observe the outcome -- an observed value is NOT challenge completion.

### `sysrupt modbus read-holding <addr> [count]`
Read holding registers (FC03)

- `<addr>` — start address
- `[count]` — number of registers
- example: `sysrupt modbus read-holding 20 2`

### `sysrupt modbus read-input <addr> [count]`
Read input registers (FC04, read-only sensors)

- `<addr>` — start address
- `[count]` — number of registers
- example: `sysrupt modbus read-input 70 5`

### `sysrupt modbus verify <addr> <expected> --input`
Read a register back and compare to an expected value

- `<addr>` — address
- `<expected>` — expected value
- `--input` — read an input register instead of holding
- example: `sysrupt modbus verify 73 100 --input`

### `sysrupt modbus write <addr> <value>` **[STATE-CHANGING]**
Write a single holding register (FC06) -- STATE CHANGING

- `<addr>` — address
- `<value>` — value 0..65535
- example: `sysrupt modbus write 20 1`
- example: `sysrupt modbus write 21 100`

> Writes one holding register. The acknowledgement is only an ack -- confirm the real effect with 'modbus verify' or by reading a status point.

## `sequence` — Optional timing/sequence engine (explicit steps only)

### `sysrupt sequence run <file> --no-timing` **[STATE-CHANGING]**
Run a sequence file (persistent connection, per-step timing)

- `<file>` — sequence JSON file
- `--no-timing` — ignore the file's delays (run as fast as possible)
- example: `sysrupt sequence run maint_override.seq.json`

> Runs ONLY the steps in the file, once each, in order. It never infers values or retries a state-changing step.

### `sysrupt sequence validate <file>`
Parse and print a sequence file without running it

- `<file>` — sequence JSON file
- example: `sysrupt sequence validate seq.json`

## `target` — View or set the badge target address

### `sysrupt target set <ip>`
Set and persist the target IP

- `<ip>` — target IP address
- example: `sysrupt target set 10.0.1.1`

### `sysrupt target show`
Show the current target and ports

## `tristation` — TriStation-lite (Safety Instrumented Bypass, Ch02)

### `sysrupt tristation download --fw <FW> --key <KEY> --trip <TRIP>` **[STATE-CHANGING]**
Program download (firmware + key + trip flag) -- STATE CHANGING

- `--fw <FW>` — firmware id
- `--key <KEY>` — debug key (read it first)
- `--trip <TRIP>` — trip function 1=on 0=off
- example: `sysrupt tristation download --fw 3008 --key 0x1a2b --trip 0`

> You must supply the firmware id, debug key and trip flag yourself. The debug key rolls over time -- read it right before downloading.

### `sysrupt tristation dump <start> <count> --watch --diff --interval <INTERVAL>`
Read a range of registers (--watch --diff for a two-snapshot diff)

- `<start>` — start register
- `<count>` — count (1..32)
- `--watch` — take two snapshots and show what changed
- `--diff` — show the diff view (requires --watch)
- `--interval <INTERVAL>` — seconds between the two snapshots (0.2..30)
- example: `sysrupt tristation dump 0 8`
- example: `sysrupt tristation dump 0 8 --watch --diff --interval 2`

> Without --watch this is the plain register dump. With --watch it captures two snapshots an interval apart and reports changed / appeared / disappeared registers (old -> new). Read-only; a register with no response is shown as unreadable, never as 0.

### `sysrupt tristation flag --base <BASE>`
Read the flag block (if the SIS exposes it)

- `--base <BASE>` — flag block base register
- example: `sysrupt tristation flag`

### `sysrupt tristation read <reg>`
Read a safety register

- `<reg>` — register number
- example: `sysrupt tristation read 0`
- example: `sysrupt tristation read 42`

### `sysrupt tristation verify <reg> <expected>`
Read a register and compare to an expected value

- `<reg>` — register
- `<expected>` — expected value
- example: `sysrupt tristation verify 0 0`

### `sysrupt tristation write <reg> <value>` **[STATE-CHANGING]**
Write a safety register -- STATE CHANGING

- `<reg>` — register
- `<value>` — value
- example: `sysrupt tristation write 1 1`
