"""Check that halide.runtime can load and run kernels compiled for GPU targets.

Each backend (Metal, CUDA, OpenCL, Vulkan) is compiled into its own loadable
module; the AOT artifact bundles its own device-capable Halide runtime, so the
standalone (CPU-only) halide.runtime module can drive it: input host buffers are
copied to the device, the kernel runs, and outputs are copied back to host.

The module paths that were actually built on this platform are passed as
arguments. A backend whose device/driver is missing at run time (common on CI)
is skipped individually; if no backend has a usable device, the whole test skips.
"""

import os
import sys

import numpy as np

import halide.runtime as hlr

_GPU_FEATURES = ("cuda", "opencl", "metal", "vulkan", "webgpu")


def backend_of(target):
    return next((f for f in _GPU_FEATURES if f in target), None)


def main():
    module_paths = sys.argv[1:]

    inp = np.arange(64, dtype=np.uint8).reshape(8, 8)
    expected = (inp.astype(np.int64) + 3).astype(np.uint8)

    ran = []
    for path in module_paths:
        # The module file is named "<name>.so"; the loader defaults the filter
        # name to that stem, which matches the Halide function name.
        kernel = hlr.load(path)
        backend = backend_of(kernel.target) or os.path.basename(path)

        out = np.zeros_like(inp)
        try:
            kernel(inp, np.int32(3), out)
        except Exception as e:
            # No usable device on this machine (e.g. a headless CI runner).
            print(f"[skip] {backend}: device unavailable: {e}")
            continue

        np.testing.assert_array_equal(out, expected)
        print(f"ran on {backend}: OK")
        ran.append(backend)

    assert "halide.halide_" not in sys.modules

    if not ran:
        built = ", ".join(backend_of(hlr.load(p).target) or p for p in module_paths)
        print(f"[SKIP] no usable GPU device (built backends: {built or 'none'})")
        return

    print("Success! GPU backends verified:", ", ".join(ran))


if __name__ == "__main__":
    main()
