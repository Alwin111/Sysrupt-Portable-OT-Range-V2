# SPDX-License-Identifier: Apache-2.0
import os
import sys

# Make the bundled `sysrupt` package importable without installation.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
