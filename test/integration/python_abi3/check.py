"""Use unchanged 3.12-built extensions on each newer regular CPython."""

import argparse
import email
import importlib
import importlib.util
import re
import sys
import types
import zipfile
from pathlib import Path

import numpy as np


def check_wheels(directory):
    for wheel in Path(directory).glob("*.whl"):
        with zipfile.ZipFile(wheel) as archive:
            metadata = email.message_from_bytes(
                archive.read(
                    next(n for n in archive.namelist() if n.endswith("/METADATA"))
                )
            )
            assert metadata["Requires-Python"] == ">=3.12", wheel
            binary_only = metadata["Name"] == "halide-bin"
            assert ("-py3-none-" if binary_only else "-cp312-abi3-") in wheel.name
            for name in archive.namelist():
                if not binary_only and name.endswith(".so"):
                    assert name.endswith(".abi3.so"), name
                if sys.platform == "win32" and name.endswith(".pyd"):
                    import pefile

                    binary = pefile.PE(data=archive.read(name))
                    imports = [
                        entry.dll.decode().lower()
                        for entry in binary.DIRECTORY_ENTRY_IMPORT
                    ]
                    assert "python3.dll" in imports, imports
                    assert not any(re.match(r"python\d{2,}", dll) for dll in imports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wheels")
    parser.add_argument("--runtime-first", action="store_true")
    parser.add_argument("--stub-first", action="store_true")
    args = parser.parse_args()
    if args.wheels:
        check_wheels(args.wheels)

    if args.stub_first:
        importlib.import_module("simplecpp_pystub")

    if args.runtime_first:
        # The regular package initializer eagerly imports the compiler. Start
        # with its namespace-package form to test runtime-first registration.
        spec = importlib.util.find_spec("halide")
        namespace = types.ModuleType("halide")
        namespace.__path__ = list(spec.submodule_search_locations)
        sys.modules["halide"] = namespace
        importlib.import_module("halide.runtime")
        del sys.modules["halide"]

    import halide as hl
    import halide.runtime as hlr
    import abi3_pipeline
    import simplecpp_pystub

    source = np.arange(6, dtype=np.uint8).reshape(2, 3)
    output = np.empty_like(source, dtype=np.float32)
    abi3_pipeline.simplecpp(source, 0.5, output)
    np.testing.assert_array_equal(output, source + 0.5)

    context = hl.GeneratorContext(hl.get_host_target())
    with context as entered:
        assert entered is context
        result = simplecpp_pystub.call(hl.Buffer(source), 0.5)
        result.realize(hl.Buffer(output))
    np.testing.assert_array_equal(output, source + 0.5)
    assert hl.BufferDimension is hlr.BufferDimension
    assert hl.Buffer(hlr.Buffer(source))[2, 1] == source[1, 2]
    print(
        f"CPython {sys.version.split()[0]}: unchanged abi3 compiler, runtime, AOT, and stub OK"
    )


if __name__ == "__main__":
    main()
