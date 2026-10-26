# Third‑party dependencies (git submodules)

This project is a fork of **Tactility OS**, which tracks several libraries as **git submodules**
(see [`.gitmodules`](.gitmodules)). This repository tracks them as submodules pinned to the exact
revisions below. **Clone recursively** so the library source is fetched:

```bash
git clone --recursive <this-repo-url>
# or, after a plain clone:
git submodule update --init --recursive
```

The `--recursive` flag matters: `mbedtls` has its own nested submodules (`framework`,
`tf-psa-crypto`, …) that its build needs.

| Submodule path | Pinned commit | Upstream URL |
|---|---|---|
| `Libraries/FreeRTOS-Kernel` | `def7d2df2b0506d3d249334974f51e427c17a41c` | https://github.com/FreeRTOS/FreeRTOS-Kernel.git |
| `Libraries/mbedtls` | `0a8fda272a5a0abef3b47c91bed37185d5a726b1` | https://github.com/Mbed-TLS/mbedtls.git |
| `Libraries/lvgl` | `e9db313f8ccc07e27f6afd0f34fe3cf6f20596f0` | https://github.com/lvgl/lvgl.git |
| `Libraries/SDL` | `39a0fab4d79ccd622512d6028ea3733bd4085455` | https://github.com/libsdl-org/SDL.git |
| `Libraries/cJSON/cJSON` | `acc76239bee01d8e9c858ae2cab296704e52d916` | https://github.com/DaveGamble/cJSON.git |
| `Libraries/minitar/minitar` | `274f9566e51b81117036dc95155f943084086f53` | https://github.com/ByteWelder/minitar.git |

These are the only paths under `Libraries/` that are submodules. The other libraries there
(`QRCode`, `esp_epaper`, `lv_screenshot`, `minmea`, and the `cJSON`/`minitar` CMake wrappers) are
small in‑tree sources committed directly, exactly as upstream Tactility keeps them.

## Updating a pin

```bash
cd Libraries/<name>
git fetch origin && git checkout <new-commit>
cd -
git add Libraries/<name> && git commit -m "Bump <name> to <new-commit>"
```

Tracking these as submodules keeps the committed repository small (~13 MB) instead of committing the
~318 MB of library source verbatim.

## ESP‑IDF managed components

`managed_components/` is **not** a submodule and is git‑ignored. `idf.py build` fetches those components
from the committed [`dependencies.lock`](dependencies.lock) manifest at build time.
