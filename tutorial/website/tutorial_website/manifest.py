"""Loads the source-file -> built-executable manifest that CMake generates.

Executable output paths depend on the generator and config and aren't known
until CMake's generate step, so tutorial/website/CMakeLists.txt writes them
out via file(GENERATE) with $<TARGET_FILE:...> rather than us trying to
compute them ourselves.
"""

from __future__ import annotations

import json
from pathlib import Path


def load_manifest(path: Path) -> dict[str, Path]:
    data = json.loads(path.read_text())
    return {name: Path(binary) for name, binary in data.items() if binary}
