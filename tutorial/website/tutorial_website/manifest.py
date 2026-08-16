"""Loads the CMake-generated manifests: source-file -> built-executable for
C++ lessons, and the Python interpreter/PYTHONPATH that can `import halide`
for Python lessons.

Executable output paths (and, for Python, the built bindings' install
location) depend on the generator and config and aren't known until CMake's
generate step, so tutorial/website/CMakeLists.txt writes them out via
file(GENERATE) with $<TARGET_FILE:...> rather than us trying to compute them
ourselves.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


def load_manifest(path: Path) -> dict[str, Path]:
    data = json.loads(path.read_text())
    return {name: Path(binary) for name, binary in data.items() if binary}


@dataclass
class PythonEnv:
    executable: Path
    pythonpath: list[Path]


def load_python_env(path: Path) -> PythonEnv:
    """WITH_TUTORIAL_WEBSITE DEPENDS on WITH_PYTHON_BINDINGS (see the
    top-level CMakeLists.txt), so this file always exists and always names a
    real interpreter -- there's no "Python bindings weren't built" case to
    degrade gracefully for."""
    data = json.loads(path.read_text())
    return PythonEnv(
        executable=Path(data["python_executable"]),
        pythonpath=[Path(p) for p in data.get("pythonpath", []) if p],
    )
