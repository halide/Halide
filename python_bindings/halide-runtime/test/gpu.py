"""Check that halide.runtime can load and run kernels compiled for GPU targets.

The active GPU target is compiled into a loadable module. The AOT artifact
bundles its own device-capable Halide runtime, so the standalone (CPU-only)
halide.runtime module can drive it: input host buffers are copied to the device,
the kernel runs, and outputs are copied back to host.

If the target's device or driver is missing at run time (common on CI), the test
skips.
"""

import os
import sys

import numpy as np

import halide.runtime as hlr

runtime_only = getattr(sys.modules["halide"], "__file__", None) is None

_GPU_FEATURES = ("metal", "opencl", "cuda", "vulkan", "webgpu")


def backend_of(target):
    return next((f for f in _GPU_FEATURES if f in target), None)


def main():
    path = sys.argv[1]

    inp = np.arange(64, dtype=np.uint32).reshape(8, 8)
    expected = inp + np.uint32(3)

    # The module file is named "<name>.so"; the loader defaults the filter name
    # to that stem, which matches the Halide function name.
    kernel = hlr.load(path)
    backend = backend_of(kernel.target) or os.path.basename(path)

    out = np.zeros_like(inp)
    try:
        kernel(inp, np.uint32(3), out)
    except Exception as e:
        # No usable device on this machine (e.g. a headless CI runner).
        print(f"[SKIP] {backend}: device unavailable: {e}")
        if runtime_only:
            assert "halide.halide_" not in sys.modules
        return

    np.testing.assert_array_equal(out, expected)

    if runtime_only:
        assert "halide.halide_" not in sys.modules

    print(f"Success! GPU backend verified: {backend}")


if __name__ == "__main__":
    main()
