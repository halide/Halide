"""Standalone Halide runtime.

Load and call precompiled Halide AOT kernels without depending on libHalide::

    import halide.runtime as hlr
    kernel = hlr.load("path/to/kernel.so")
    kernel(input_array, output_array)

This subpackage links only the header-only Halide runtime, so it can be used in
deployment environments that do not have the Halide compiler installed.
"""

from ._runtime import Buffer, BufferDimension, Kernel, Type, load

__all__ = ["Buffer", "BufferDimension", "Kernel", "Type", "load"]
