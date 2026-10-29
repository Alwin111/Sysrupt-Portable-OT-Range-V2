# `source/ot/net/` — protocol servers & web

The network layer: the industrial-protocol listeners and the `:80` HTTP server, plus the embedded player
toolkit.

## The `:80` web server

| File | Serves | Paths |
|------|--------|-------|
| `OtHttp.*` | Operator **reactor HMI** + shared web design system + login/control | `/`, `/hmi`, `/login`, `/control`, `/api/state`, `/plant.webp` |
| `DefWeb.*` | **Corporate intranet, IR webmail, inventory, submit** | `/intranet`, `/mail`, `/inventory`, `/investigate`, `/evidence`, `/submit` |
| `HistWeb.*` | **Process historian console** (the #07 SQLi surface) | historian console + query |
| `ToolkitZip.*` | The embedded `sysrupt` toolkit archive | `/toolkit.zip` |
| `PlantImg.*` | The animated plant process image shown in the HMI | `/plant.webp` |

`OtHttp.h` defines `kWebStyle`, the shared light design system used by all three web files so the whole
HMI reads as one product.

## Protocol listeners

Modbus/TCP (502), IEC-104 (2404), TriStation-lite (1502), OPC-UA-lite (9005), pump radio (7700),
GOOSE (UDP 6180) and SNMP (UDP 161). Each is bridged to a `../sim` plant by a `Service` wrapper in
[`../`](..). The historian is a web system served under `/hist` on :80 (see `HistWeb`), not a listener.

## Embedded toolkit

`ToolkitZip.cpp` holds the `sysrupt` release ZIP as a byte array, generated from the deterministic
release archive. A host test (`ot_toolkitzip_tests`) asserts the embedded bytes equal the release ZIP
**byte-for-byte**, so the download and the firmware copy are provably identical.

## Notes for contributors

- The ESP32-S3 has **no PSRAM**; keep handler buffers small and serve pages in chunks
  (`httpd_resp_sendstr_chunk`).
- The server marks responses `Connection: close` (`setNoKeepAlive`) because the socket pool is tiny.
- Self-closing inline SVG elements must quote the last attribute or put a space before `/>` (an unquoted
  last attribute eats the `/`), or the element won't render.
