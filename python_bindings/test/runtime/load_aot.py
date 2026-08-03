"""Test the standalone `halide.runtime` loader against a precompiled AOT kernel.

The key property under test is that `halide.runtime` can load and call a
precompiled Halide kernel WITHOUT importing the Halide compiler package (and thus
without libHalide): `import halide.runtime` must not pull in `halide.halide_`.
"""

import sys

import numpy as np

import halide.runtime as hlr


def main():
    aot_path = sys.argv[1]

    # Importing the runtime must not have loaded the compiler extension.
    assert "halide.halide_" not in sys.modules, (
        "importing halide.runtime pulled in the halide compiler extension"
    )

    kernel = hlr.load(aot_path, name="runtimeadd")
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
    assert buf.dimensions == 2 and buf.type == "uint8" and buf.shape == [2, 3]
    assert (
        isinstance(buf._get_raw_halide_buffer_t(), int)
        and buf._get_raw_halide_buffer_t() != 0
    )
    view = np.asarray(buf)
    assert np.array_equal(view, grid)
    view[0, 0] = 42
    assert grid[0, 0] == 42, "Buffer must be a zero-copy view of its source"

    # Pass Buffer objects to the kernel (via the _get_raw_halide_buffer_t bridge).
    out3 = np.zeros_like(rng)
    kernel(hlr.Buffer(rng), np.int32(7), hlr.Buffer(out3))
    expected3 = ((rng.astype(np.int64) + 7) % 256).astype(np.uint8)
    assert np.array_equal(out3, expected3), (out3, expected3)

    # Still no compiler after all of this.
    assert "halide.halide_" not in sys.modules

    print("Success!")


if __name__ == "__main__":
    main()
