"""Test the standalone `halide.runtime` loader against a precompiled AOT kernel.

The key property under test is that `halide.runtime` can load and call a
precompiled Halide kernel WITHOUT importing the Halide compiler package (and thus
without libHalide): `import halide.runtime` must not pull in `halide.halide_`.
"""

import sys
from pathlib import Path

import numpy as np

import halide.runtime as hlr


def main():
    aot_path = sys.argv[1]

    # Importing the runtime must not have loaded the compiler extension.
    assert "halide.halide_" not in sys.modules, (
        "importing halide.runtime pulled in the halide compiler extension"
    )

    # load() accepts the standard os.PathLike protocol.
    kernel = hlr.load(Path(aot_path), name="runtimeadd")
    assert kernel.name == "runtimeadd", kernel.name
    assert kernel.argument_names == ["input", "offset", "output"], kernel.argument_names

    rng = np.arange(0, 256, dtype=np.uint8)
    out = np.zeros_like(rng)

    # positional
    kernel(rng, np.int32(5), out)
    expected = ((rng.astype(np.int64) + 5) % 256).astype(np.uint8)
    assert np.array_equal(out, expected), (out, expected)

    # keyword + mixed
    out2 = np.zeros_like(rng)
    kernel(rng, output=out2, offset=200)
    expected2 = ((rng.astype(np.int64) + 200) % 256).astype(np.uint8)
    assert np.array_equal(out2, expected2), (out2, expected2)

    # halide.runtime.Buffer interop: introspection + zero-copy NumPy round-trip.
    grid = np.arange(6, dtype=np.uint8).reshape(2, 3)
    buf = hlr.Buffer(grid)
    assert buf.dimensions() == 2 and str(buf.type()) == "uint8"
    assert [buf.dim(i).extent() for i in range(buf.dimensions())] == [3, 2]
    assert type(buf._get_halide_buffer_t_capsule()).__name__ == "PyCapsule"
    view = np.asarray(buf)
    assert np.array_equal(view, grid)
    view[0, 0] = 42
    assert grid[0, 0] == 42, "Buffer must be a zero-copy view of its source"

    # Pass Buffer objects to the kernel (via the named-capsule bridge).
    out3 = np.zeros_like(rng)
    kernel(hlr.Buffer(rng), np.int32(7), hlr.Buffer(out3))
    expected3 = ((rng.astype(np.int64) + 7) % 256).astype(np.uint8)
    assert np.array_equal(out3, expected3), (out3, expected3)

    # Exceptions raised by the capsule protocol must not be discarded in favor
    # of an attempted buffer-protocol fallback.
    class CapsuleError(Exception):
        pass

    class BrokenCapsuleProvider:
        def _get_halide_buffer_t_capsule(self):
            raise CapsuleError("capsule failure")

    try:
        kernel(BrokenCapsuleProvider(), 1, bytearray(1))
    except CapsuleError as e:
        assert str(e) == "capsule failure"
    else:
        assert False, "capsule protocol exception was suppressed"

    # Preserve the buffer exporter's useful error for a read-only output.
    try:
        kernel(bytearray(1), 1, bytes(1))
    except BufferError as e:
        assert "writable" in str(e)
    else:
        assert False, "read-only output was accepted"

    # Halide strides are measured in elements, so byte strides must divide
    # evenly by the item size.
    overlapping = np.ndarray((2,), dtype=np.int32, buffer=bytearray(8), strides=(1,))
    try:
        hlr.Buffer(overlapping)
    except ValueError as e:
        assert "stride" in str(e)
    else:
        assert False, "non-integral element stride was accepted"

    # Multi-byte buffers must use native byte order.
    try:
        hlr.Buffer(np.zeros(1, dtype=">i4"))
    except ValueError as e:
        assert "byte order" in str(e)
    else:
        assert False, "non-native-endian buffer was accepted"

    # Still no compiler after all of this.
    assert "halide.halide_" not in sys.modules

    print("Success!")


if __name__ == "__main__":
    main()
