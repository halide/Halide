# NVIDIA `libdevice` bitcode

`libdevice.10.bc` is NVIDIA's device-side math library (`sin`, `exp`, `sqrt`,
etc.), distributed as a single, architecture-generic LLVM bitcode module as part
of the CUDA Toolkit. It is vendored here verbatim — Halide does not build it —
and is linked into every GPU kernel module produced by Halide's CUDA/PTX device
codegen. See:

- `src/CodeGen_PTX_Dev.cpp` — `__nvvm_reflect` / `nvvm-reflect-ftz` handling
  that libdevice relies on for its accuracy-vs-speed switches.
- `src/LLVM_Runtime_Linker.cpp` — links this module into the PTX device initial
  module.
- `Makefile` / `src/runtime/CMakeLists.txt` — run the file through `binary2cpp`
  to embed it as a byte array in the compiler binary.

## Updating this file

`libdevice.10.bc` lives at `nvvm/libdevice/libdevice.10.bc` inside any CUDA
Toolkit install (e.g. `$CUDA_HOME/nvvm/libdevice/libdevice.10.bc`). To update
the vendored copy, just copy that file over this one — no other files in this
directory need to change. The current copy was taken from CUDA Toolkit 11.6.

## License

This file is distributed under, and its use governed by, NVIDIA's CUDA Toolkit
End User License Agreement: https://docs.nvidia.com/cuda/eula
