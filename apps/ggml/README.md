# kernel-bench

Benchmarks GGML's CPU quantize / dequantize / vec_dot / repack kernels against a
designated correctness reference, and is structured so that a from-scratch
implementation of any of those kernels can be dropped in and compared too. See
`providers/README.md` for how to add one.

This is a **standalone** CMake project. It is not built as part of GGML itself
and consumes an already-built-and-installed GGML purely as an external
dependency via `find_package`.

## Build

```sh
# 1. Build and install GGML somewhere (skip if you already have an install).
#    GGML_BACKEND_DL=OFF (the default) is required -- see the "private ABI" note below.
cmake -S /path/to/ggml -B /path/to/ggml/build -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/ggml/build -j
cmake --install /path/to/ggml/build --prefix /path/to/ggml/install

# 2. Build kernel-bench against that install.
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/ggml/install
cmake --build build -j

./build/kernel-bench --all
```

## What the report means

Each row shows a `ggml_type` (or, for repack, a specific interleave layout like
`q4_0_4x4_q8_0`), GGML's designated **reference** implementation and its
timing/throughput, and one column per **candidate** implementation registered
for that kernel: its timing/throughput, speedup relative to the reference, and
whether its output matched the reference (quantize/repack packing is checked
byte-for-byte; dequantize/vec_dot/gemv/gemm results are checked within a
relative-error tolerance, since those involve floating point accumulation that
different implementations may order differently).

A candidate flagged "identical to reference" has the exact same function address
as the reference -- this happens whenever the current CPU architecture has no
separate optimized kernel for that type (GGML's `src/ggml-cpu/arch-fallback.h`
collapses the two names onto one symbol in that case), so timing it separately
would only measure noise.

The dequantize table currently shows only a reference column with no candidates:
GGML has exactly one dequantize implementation per type (arch-independent, in
`src/ggml-quants.c`), so there's nothing to compare it against yet -- this is
intentionally the first place to plug in a new provider (see
`providers/README.md`).

## Why this needs a private ABI header

GGML's public API (`ggml_get_type_traits` / `ggml_get_type_traits_cpu` in
`include/ggml.h` / `include/ggml-cpu.h`) exposes exactly one reference and one
CPU-dispatched implementation per type, which is sufficient for the quantize and
dequantize benchmarks without touching anything private. It does **not** expose
the always-available pure-C fallback for `vec_dot`, nor anything for the repack
`quantize_mat`/`gemv`/`gemm` kernels. Those are only reachable because
`ggml-cpu` is built without `-fvisibility=hidden`, so its internal (but
non-`static`) C symbols end up with default/exported linker visibility by
accident of the build configuration rather than by design.
`providers/ggml_internal_abi.h` redeclares exactly the symbols needed, copied
from GGML's uninstalled `src/ggml-cpu/quants.h` / `repack.h` as of the commit
this tool was written against. If a future GGML release renames or changes the
signature of one of these functions, that header (and
`providers/ggml_provider.cpp`) are the only places that need updating.
