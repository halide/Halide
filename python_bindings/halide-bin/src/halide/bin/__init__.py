import ctypes
import os
from pathlib import Path


def install_dir():
    # CMake's binary payload is installed under the shared ``halide`` package.
    return str(Path(__file__).resolve().parents[1])


_root = Path(install_dir())
_bin_dir = _root / "bin"
if hasattr(os, "add_dll_directory") and _bin_dir.is_dir():
    os.add_dll_directory(str(_bin_dir))
for _relpath in (
    "bin/Halide.dll",
    "lib/libHalide.dylib",
    "lib64/libHalide.so",
    "lib/libHalide.so",
):
    _library = _root / _relpath
    if _library.exists():
        ctypes.CDLL(str(_library))
        break
