#!/usr/bin/env python3
"""Entry point for generating the Halide tutorial's Sphinx pages.

See tutorial_website/cli.py for the actual implementation; this thin wrapper
just makes sure the package is importable when run directly (e.g. from
doc/CMakeLists.txt) without needing to install it.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from tutorial_website.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
