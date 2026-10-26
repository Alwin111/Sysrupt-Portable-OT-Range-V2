# Authenticated download integration — IMPLEMENTED

The distribution archive `release/sysrupt-toolkit-<ver>.zip` is served from the badge at
`GET /toolkit.zip`, behind the existing engineer session. This is now implemented in firmware (it
was previously only a plan). Summary of what shipped:

- `tools/sysrupt/gen_embedded_zip.py` turns the release ZIP into `source/ot/net/ToolkitZip.{h,cpp}`
  (a `const unsigned char toolkit_zip[]` in flash `.rodata`, plus `toolkit_zip_len` and
  `toolkit_zip_sha256`) — the same embedded-asset convention as `ReactorImg.cpp`.
- `handleToolkitDownload` in `source/ot/net/OtHttp.cpp` gates on `validSession(req)` (== the exported
  `engineerSessionValid`), returns `401 Unauthorized` without a session, and otherwise streams the
  bytes in 1 KB `httpd_resp_send_chunk` chunks straight from `.rodata` (no second copy in RAM), with
  `Content-Type: application/zip`, `Content-Disposition: attachment; filename="sysrupt-toolkit.zip"`
  and an `X-Toolkit-SHA256` header.
- The route is registered as `makeUri("/toolkit.zip", HTTP_GET, handleToolkitDownload)`; the intranet
  HOME page links to it.
- `test/ot_toolkitzip_tests.cpp` asserts the embedded asset is byte-for-byte identical to the release
  archive.

To refresh after changing the toolkit: rebuild the ZIP, run `gen_embedded_zip.py`, re-run the host
tests (the integrity test will catch a stale asset), rebuild the firmware, and reflash.

The original integration options are retained below for reference.

## What already exists (verified in source)

- The badge runs one HTTP server on `:80` owned by `OtHttp` (`source/ot/net/OtHttp.cpp`).
- Authentication is a tiny in-memory session table (`g_sessions`), established by `POST /login`
  (`handleLogin`, fields `user`/`pass`) which sets a session cookie.
- `engineerSessionValid(req)` validates that cookie and is already **exported for `DefWeb`**, so a
  new route can gate on the same session as `/hmi` and the intranet.
- Routes are registered with `makeUri(...)`; `config.max_uri_handlers = 20` and roughly 13 are in
  use, so there is room for one more.
- The server sends bodies with `httpd_resp_sendstr_chunk` / `httpd_resp_send`, and the device has
  only ~3.4 KB free internal heap, so a download must be **streamed in small chunks**, never built
  in one buffer.

## Recommended implementation (when approved)

1. **Embed the archive** as a `const unsigned char[]` in a generated source file, e.g.
   `source/ot/net/ToolkitZip.cpp`, produced at build time from `tools/sysrupt/release/*.zip` by a
   small CMake `add_custom_command` (xxd-style). ~52 KB lands in flash `.rodata`; the app partition
   is currently 49 % free, so there is ample room. This keeps it off the heap and off the FAT
   partition, and makes the download reproducible with the firmware image.
2. **Add one gated route** in `OtHttp` (or `DefWeb`), e.g. `GET /toolkit` / `GET /toolkit.zip`:
   ```cpp
   esp_err_t handleToolkitDownload(httpd_req_t* req) {
       if (!engineerSessionValid(req)) { /* 302 -> /login */ return ESP_OK; }
       httpd_resp_set_type(req, "application/zip");
       httpd_resp_set_hdr(req, "Content-Disposition",
                          "attachment; filename=\"sysrupt-toolkit.zip\"");
       const size_t CH = 1024;                     // small chunks: gentle on the heap
       for (size_t off = 0; off < TOOLKIT_ZIP_LEN; off += CH) {
           size_t n = (TOOLKIT_ZIP_LEN - off) < CH ? (TOOLKIT_ZIP_LEN - off) : CH;
           if (httpd_resp_send_chunk(req, (const char*)TOOLKIT_ZIP + off, n) != ESP_OK)
               return ESP_FAIL;                    // client dropped -> stop
       }
       return httpd_resp_send_chunk(req, nullptr, 0);
   }
   ```
3. **Link it from the authenticated UI** — add a "Download toolkit" link on `/hmi` or `/intranet`
   (both already behind the session), pointing at `/toolkit.zip`.
4. **Publish a checksum** — print the archive's SHA-256 on the page so players can verify the
   download; generate it in the same CMake step.

## Explicitly out of scope / not done

- No change to the login system, session handling, or any access control.
- No endpoint invented or claimed as live.
- The archive is not embedded in the current firmware image.

## Fallback distribution (works today, no firmware change)

Serve `release/sysrupt-toolkit-<ver>.zip` from the existing range provisioning host / laptop over
the badge network, or hand it to players directly. The archive is self-contained and needs only
Python 3.9+.
