# x86 / CUDA log

## Phase 0: environment and gate

Machine: Xeon w5-2455X (Sapphire Rapids, 12C/24T: AVX-512, VNNI, BF16, FP16,
AMX), RTX A4500 (sm_86), WSL2 Ubuntu 24.04, CUDA 13.2, LLVM 22. GGML 0.26.0 via
`apps/vcpkg` with the `cuda` feature (native ISA incl. AMX, CPU repack,
`GGML_LLAMAFILE=OFF`, CUDA for the native arch).

Gate: `ggml-quant-bench --check`: 2276 results, 0 failures (468 vec_dot,
1164 mul_mat, 644 codec; 1900 Halide rows, 376 GGML rows incl. 108 ggml-cuda
across mmvq/mmq/mmvf/mmf/cuBLAS and 24 ggml-cpu-amx). `ctest`: 11/11 pass.

Core fixes:

- `ExtractTileOperations`: the AMX pass rewrote `Tile` allocations inside GPU
  kernels (wmma); it now leaves GPU kernels alone.
- `Parameter`: a struct buffer's default host alignment was the struct's size
  (e.g. 64 bytes), which let x86 codegen emit aligned 64-byte loads on
  2-byte-aligned records and fault; it is now the struct's alignment.
- CUDA runtime: dlopen `libcuda.so.1` first; WSL ships only the soname.

App fixes:

- `blocked()` vectors are capped at 16 bytes (records are not over-aligned).
- `--probe-gpu`: CUDA kernel attribution in a child process via CUPTI.
- Schemes' `a*b+c` in GGML's reference quantizers rounds as GGML's build does:
  `ggml-base` is built for the baseline ISA, which on x86-64 has no FMA, so it
  is unfused there (fused on arm64). Without this, q6_K's scales differ by one.
- GGML's AMX buffer runs f16 on AVX-512 (no repack); labelled `amx:f16(avx512)`.

Known, unrelated: core `correctness_constant_blob_placement` segfaults in
libstdc++'s `std::regex` (recursion depth), before and after these changes.

## Phase 1: x86 CPU

Core changes (each with its test; none upstreamed yet):

- `CodeGen_X86`: signed int8 x int8 4-way dots select `vpdpbusd` (unsigned
  where an operand is known non-negative, else a bias of 128 corrected by the
  other's sums, as GGML does). Unblocked VNNI for every integer block dot.
  Test: `simd_op_check_x86`.
- `x86`: Sapphire Rapids implies AVX512-FP16, so scalar f16 loads are `vmovsh`,
  not F16C's merging `vpinsrw`, which serialized reductions (q8_0 vec_dot: 11
  cycles per block). Test: `simd_op_check_x86`.
- `lossless_cast`: a float32 built from a bfloat16's bits (shift by 16) is that
  bfloat16, so bf16 dots reach `vdpbf16ps`. Test: `simd_op_check_x86`'s
  `vdpbf16ps` checks, `lossless_cast`.
- `constant_integer_bounds`: `a & b` with a non-negative operand lies in
  [0, its max]. Unblocked `vpdpbusd` for masked 2-bit codes (tq2_0: 12 to 8
  dot instructions per iteration). Tests: `constant_interval`, `lossless_cast`
  fuzz generator.
- `CodeGen_X86`: a 4-way dot of three byte factors, one uniform over lane pairs
  (a sub-block scale), is `pmaddubsw` of the other two then `pmaddwd` by the
  scale (GGML's AVX2 q2_K/q6_K), not a byte multiply by the scale first.
  Test: `simd_op_check_x86`.

App changes:

- VNNI block dots on 32-byte lanes and steps; 16-value sub-blocks paired per
  vector; bf16 vec_dot by `vdpbf16ps` pairs; q2_0 (new format) as GGML's x86
  build converts its non-finite codes.
- AMX gemm (`blocked_amx()`, M >= 16) for integer weights of one row per record
  with no sub-blocks or affine terms: the weights decoded in rows then
  transposed into AMX's 4-byte interleave, once per block for up to 4 tiles
  of M; one `tdpbssd` per 64 bytes; per-block scales in f32 on AVX-512.
- With AVX-512 VNNI, 16-value sub-blocks decoded without tables (q2_K, q6_K)
  take a zmm, each sub-block's scale in its lanes' products (the
  `pmaddubsw`/`pmaddwd` rewrite above): one-thread vec_dot 0.45 -> 0.77x of
  GGML for q2_K, 0.48 -> 0.79x for q6_K; gemv 0.47 -> 0.81x, 0.83 -> 1.08x.
  The q6_K offset sums 16 lanes a step on x86 (a ymm `pmaddwd`).
- Bench: `--early-stop[=LO,HI]` ends a case's rounds once every GGML/Halide
  ratio's 95% CI is outside [LO, HI]; the main thread's AMX tile config is
  kept across Halide calls (GGML's AMX kernels load theirs once per thread).
