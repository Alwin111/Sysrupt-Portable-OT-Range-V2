#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the self-contained player release archive release/sysrupt-toolkit-<ver>.zip.

Deterministic (sorted entries + a fixed timestamp) so the byte-for-byte integrity test
(test/ot_toolkitzip_tests.cpp) stays stable. After running this, regenerate the embedded C asset:

    python3 make_release.py
    python3 gen_embedded_zip.py release/sysrupt-toolkit-1.0.0.zip

Ships the runnable toolkit (package + launcher + examples + its tests + docs); it deliberately omits
the dev/integration tooling (this script, gen_commands.py, gen_embedded_zip.py), VCS files, caches and
the release/ directory itself. Standard library only.
"""
from __future__ import annotations

import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sysrupt import __version__            # noqa: E402

VERSION = __version__                      # the test path pins 1.0.0; bump both together if changed
ROOT = f"sysrupt-toolkit-{VERSION}"
OUT = os.path.join(HERE, "release", f"{ROOT}.zip")
FIXED_DATE = (2026, 1, 1, 0, 0, 0)         # fixed mtime -> reproducible archive

# Dev/integration tooling and junk that must NOT ship to players.
EXCLUDE_FILES = {"make_release.py", "gen_commands.py", "gen_embedded_zip.py", ".gitignore"}
EXCLUDE_DIRS = {"release", "__pycache__", ".git"}
INCLUDE_TOP = {"pyproject.toml", "sysrupt.example.json",
               "README.md", "INSTALL.md", "COMMANDS.md", "NOTICE.md", "DOWNLOAD_INTEGRATION.md"}


def _collect() -> list:
    files = []
    for dirpath, dirnames, filenames in os.walk(HERE):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
        for fn in filenames:
            if fn.endswith(".pyc"):
                continue
            abspath = os.path.join(dirpath, fn)
            rel = os.path.relpath(abspath, HERE).replace(os.sep, "/")
            top = rel.split("/", 1)[0]
            if "/" not in rel:                       # a top-level file
                if fn in EXCLUDE_FILES or fn not in INCLUDE_TOP:
                    continue
            elif top in ("sysrupt", "tests", "bin", "examples"):
                pass                                 # ship these trees
            else:
                continue
            files.append(rel)
    return sorted(files)


def main() -> int:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    rels = _collect()
    with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for rel in rels:
            data = open(os.path.join(HERE, rel), "rb").read()
            info = zipfile.ZipInfo(f"{ROOT}/{rel}", date_time=FIXED_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            # Preserve the executable bit for the launcher; 0644 for everything else.
            is_exec = rel == "bin/sysrupt" or os.access(os.path.join(HERE, rel), os.X_OK)
            info.external_attr = ((0o755 if is_exec else 0o644) & 0xFFFF) << 16
            z.writestr(info, data)
    size = os.path.getsize(OUT)
    print(f"wrote {OUT} ({size} bytes, {len(rels)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
