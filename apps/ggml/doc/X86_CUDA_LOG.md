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
- `CodeGen_X86`: the high half of a u8 x u8 product (`mul_shift_right`, or a
  narrowed `widening_mul >> 8..15`) is `pmulhuw` on the even and odd bytes in
  place, not unpack/multiply/pack. tq1_0's base-3 digits, `(q * 3) >> 8`.
  Test: `simd_op_check_x86`.
- `OptimizeSmallTableLookups`: on x86, a gather whose lanes are lane 0's index
  plus constants is a dense load of that span and a shuffle (safe as
  `stage_strided_loads` is: it reads only between elements it reads), not a
  `pextr`/`pinsr` per lane. tq1_0's last 16 values from 4 bytes; with the
  above, vec_dot 62.5 -> 30.9 cycles a block (GGML 30.7), one-thread vec_dot
  0.34 -> 0.70x of GGML, gemv 0.37 -> 0.66x. Test: `small_table_lookup`.
- Value bounds of `precompute()`d Funcs from their Buffers' contents, kept
  through precomputation (keyed by buffer name) and read by `ConstantBounds`.
  `FindIntrinsics` marks an int8 Mul's operands with `unsafe_promise_clamped`
  when one factor is in [-1, 1] (stripped by `lower_intrinsic` elsewhere);
  x86 makes it `psignb`, and the signed dot of such codes
  `dot(abs(a), psign(b, a))` (`vpdpbusd`). The iq formats' `grid * sign`
  codes, as GGML builds them; one-thread gemv (q8_K acts) iq2_xxs 0.67 ->
  0.78x, iq2_xs 0.69 -> 0.81x, iq2_s 0.47 -> 0.67x, iq3_xxs 0.54 -> 0.59x,
  iq3_s 0.72 -> 0.92x, iq1_s 0.77 -> 0.82x, iq1_m 0.66 -> 0.69x, q3_K 0.79 ->
  0.80x. Not when an operand is itself a sign (q1_0's bits): the bias form's
  correction is shared across a gemm tile (q1_0 gemm 2.1 -> 1.7 ms, 12
  threads). Test: `simd_op_check_x86`.
- `FindIntrinsics`: an int8 operand of a `widening_mul` loaded from a Func
  whose value bounds are tighter than its own (int8's) is marked with
  `unsafe_promise_clamped` carrying them, so a staged Func of non-negative
  bytes (decoded weights) is `vpdpbusd`'s unsigned operand, not the bias form.
  q4_K's column gemm: 120 -> 83 cycles a sub-block (llvm-mca). Test:
  `x86_dot_product_loaded_bounds`.

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
- `formats.cmake` rows may lead with `[x86]`/`[arm]`, applied only for that
  host CPU and overriding the untagged (M3) rows' same parameters. On x86,
  iq2_xs, iq2_s and iq3_s build with `slp=false` (SLP packed their grid
  indices, then extracted each with `vpextrw`): one-thread gemv 0.41 -> 0.69x,
  0.36 -> 0.47x, 0.48 -> 0.71x of GGML; vec_dot 0.35 -> 0.50x, 0.29 -> 0.50x,
  0.47 -> 0.71x.
- `Knobs::plain_index_words` (default true, the M3 behavior): false reads each
  grid index byte in place rather than staging the index words in registers
  (LLVM extracted each byte with shift and mask, and the words spilled). On
  x86, iq3_xxs takes it, and its gemv `sublanes=false` (rows were built on the
  stack): 103 -> 79 cycles a block in vec_dot, 101 -> 76 in gemv; one-thread
  gemv 0.59 -> 0.89x of GGML, all-core 0.66 -> 0.92x, vec_dot 0.67 -> 0.85x.
- Bench: GGML CPU mul_mat is labelled `tiled:<type>` where GGML 0.26's tiled
  gemm (`ggml-cpu/tiled`, x86, M >= 8, K-quants, iq4_xs and the iq2/iq3/iq1
  formats; off with `GGML_CPU_TILED_MM=0`) takes it, not `vec_dot:`. It decodes
  a panel of weights once to biased bytes and runs a 16x16 VNNI microkernel
  over columns: about 9 cycles a block-dot against our 36 for iq3_xxs (4096^2,
  M = 32, one thread: GGML 4.9 ms, without tiled 41.7 ms, ours 20.3 ms).
- `Knobs::gemm_wide_subs` (default true): false keeps gemm tiles' 16-value
  sub-blocks on ymm steps (gemv/vec_dot keep the zmm). On x86, q6_K takes it
  (4096^2, M = 32, 12 threads: 1.91 -> 1.4-1.5 ms); q2_K is faster wide
  (1.16 ms against 1.97).
- `blocked_cols()`: with AVX-512 VNNI, gemm tiles (M >= 16) of 256-value
  blocks vectorized across 16 columns (M), as GGML's tiled 8x16 microkernel:
  each weight byte group broadcast against 16 columns' act bytes (staged
  transposed, once), 8 rows unrolled, the block's accumulators, staged weights
  and the act's bsums all stored columns-innermost. The weights are decoded
  once per block by a per-tile `clone_in` (tables shared across the cols32
  and cols16 specializations were scheduled twice). The d.sums (mins) stages
  unroll every RVar so their port reads CSE across rows. One thread, 4096^2,
  M = 32, ms (GGML in brackets): q4_K 11.1 -> 4.1 (4.3), q2_K 11.1 -> 5.1
  (4.9), q5_K 13.3 -> 5.3 (4.9), q3_K 15.5 -> 8.6 (4.9), q6_K 14.8 -> 9.9
  (6.0), iq4_xs 13.7 -> 6.2 (3.4), iq2_xxs 18.0 -> 6.8 (4.6), iq2_xs 19.5 ->
  7.2 (5.4), iq2_s 21.7 -> 7.7 (5.5), iq3_xxs 22.9 -> 7.2 (5.1), iq3_s 29.5 ->
  8.0 (5.5), iq1_s 19.1 -> 9.9 (5.0), iq1_m 21.1 -> 10.7 (6.5); f32 acts
  alike.
- Offset formats in `blocked_cols()`: `offset_codes()` splits a weight's
  decoded codes `base + c` (q6_K's -32, q3_K's, q2_K's) into a staged unsigned
  Func `u` and `u + c`, so the staged weights feed plain `vpdpbusd` and the
  offset folds through the act's bsums, as GGML's tiled gemm does (before:
  signed staged codes, each dot `vpbroadcastd` + 2 `vpsignb` +
  `vinserti64x4`, SPR having no zmm `vpsignb`). The cols gemm always folds,
  whatever `fold_offsets` says. The staging splits K by the vector width and
  unrolls the outer part (constant sub-block shifts, not per-lane u32
  shifts), except for table-decoded formats (iq3_s 3% slower unrolled). One
  thread, 4096^2, M = 32, ms (GGML): q6_K 9.2 -> 4.7 (5.9), q3_K 8.6 -> 5.7
  (5.1), q2_K 5.0 -> 4.4 (5.1), q5_K 4.8 -> 4.1 (4.0), q4_K 3.7 (3.6); iq
  formats unchanged.
- Signed codes without an offset (the iq formats' grid values) in
  `blocked_cols()`: `offset_codes(..., bias)` stages them biased to unsigned
  bytes (u = q + 128, from q's type bounds), the -128 folded through the
  act's bsums, as GGML's tiled gemm. One thread, 4096^2, M = 32, ms (GGML):
  iq2_xxs 6.7 -> 4.3 (4.6), iq2_xs 7.2 -> 5.2 (5.5), iq2_s 7.7 -> 6.7 (5.5),
  iq3_xxs 7.2 -> 4.8 (4.9), iq3_s 8.1 -> 5.8 (5.3), iq4_xs 6.3 -> 4.1 (3.3);
  iq1_s/iq1_m unchanged (9.5, 10.7 against 4.9, 6.5).
- iq1_s/iq1_m in `blocked_cols()`: their codes simplify to (8 g - 2 s) + 1,
  an offset whose rest is signed; biased instead (from the promised [-9, 9]
  bounds, u = q + 9). One thread, 4096^2, M = 32, ms (GGML): iq1_s 9.5 ->
  5.0 (5.0), iq1_m 10.7 -> 6.3 (6.4).
- The act bsum reduction in `schedule_sums()` vectorized at 16 int32 lanes
  (zmm `vpmovsxwd` + `vpaddd`) inside the ymm gemv loop; on SPR a zmm uop
  takes port 1's vector ALUs offline, which llvm-mca doesn't model (tq2_0:
  12.7 cycles per block predicted, 15.9 measured). Now split to the loop's
  width and unrolled. One thread, 4096^2, M = 1, GGML / halide: tq1_0 0.66
  -> 1.03, tq2_0 0.71 -> 1.08, q6_K 0.90 -> 1.16; others unchanged.
- Raw sign bits (iq2_s, iq3_s; iq1's): x86 has no byte shifts, so a bit at
  a constant per-lane position, (x >> c) % 2, took seven ops (LLVM widens to
  words, shifts, packs), and its 1 - 2 b went through psignb (a factor in
  [-1, 1]), which LLVM can't fold. CodeGen_X86 now tests x & (1 << c)
  (vptestnmb) and multiplies by a factor in [0, 1] as a mask, which LLVM
  folds into one blend. One thread, 4096^2, GGML / halide, M = 1: iq2_s
  0.66 -> 0.82, iq3_s 0.87 -> 1.07, iq1_m 0.72 -> 0.78; M = 32, ms (GGML):
  iq2_s 6.6 -> 6.3 (5.7), iq3_s 5.8 -> 5.6 (5.3), iq1_m 6.3 -> 5.3 (6.4).
- q5_0/q5_1's codes (a staged table row of the high bits plus the low
  nibble) reach FindIntrinsics already as an explicit `widening_mul`, which
  didn't get the loaded bounds of `with_loaded_bounds()`; it does now, so
  their [0, 31] codes are vpdpbusd's unsigned operand, not the bias form.
  One thread, 4096^2, M = 1, GGML / halide: q5_0 0.86 -> 0.93, q5_1 0.82 ->
  0.90.
- q5_0's AMX gemm (`blocked_amx()`) gathered its table's bytes per code
  (480 vpinsrb in the block loop); its rows are now staged per block
  (`tables()`, on a clone of the decode for each tile size). One thread,
  4096^2, M = 32: 42.1 -> 5.4 ms (GGML 23.3).

## Phase 2: CUDA (RTX A4500, sm_86, WSL2; ncu unavailable: ERR_NVGPUCTRPERM)

- Baseline (4096^2, us, Halide vs GGML): f16 M = 1 77.6 vs 78.3 (cuBLAS),
  M = 8 153 vs 78.6, M = 32 181 vs 81.7, M = 512 1025 vs 294; q4_0:f16 on
  f16 acts M = 1 30.1, M = 8 102, M = 32 319, M = 512 854 (GGML q4_0 on f32
  acts: mmvq 31.4, 46, mmq 55.5, 325).
- `mul_mat()`'s GPU dispatch is Metal's on CUDA too (wmma's 16 x 16
  fragments): blocked weights take `metal_rows()`. With integer acts their
  codes' dot is int32, each lane's run of 8 values a 4-way vector reduction
  (PTX: dp4a), and every M takes the gemv's lanes, tiled over columns
  (Metal's lane per value at M > 1 is float per value). CUDA rows: q4_0,
  q8_0 (f32, q8_0 acts), q4_K (f32, q8_K). 4096^2, us, Halide (q8 acts) vs
  GGML (f32 acts, mmvq: quantized to q8_1 inside):
  M = 1 q4_0 22.7 vs 28.1, q8_0 36.9 vs 45.4, q4_K 27.8 vs 36.1;
  M = 8 q4_0 55.4 vs 48.6, q8_0 65.7 vs 55.1, q4_K 108 vs 57.1.
  M >= 32 (mmq) remains far off: q4_0 185 vs 67 (M = 32), 3121 vs 353
  (M = 512).
- The CUDA gemm's staging loads were scalar (`ld.global.nc.b16`): the
  operands' alignment was unknown past their elements'. CUDA libraries now
  promise 16-byte aligned buffers (`set_host_alignment`; cudaMalloc's 256,
  GGML's CUDA buffers 128): `ld.global.nc.v2.b32`. And the shared copies'
  rows were 64 halves (128 bytes: wmma's row loads 8-way bank conflicts);
  on CUDA they're padded by 8 (144 bytes), a block's rows adjacent. 4096^2,
  us (cuBLAS): f16 M = 32 181 -> 99 (77), M = 512 1025 -> 409 (303);
  q4_0:f16 M = 32 319 -> 185, M = 512 854 -> 465.
- On CUDA a lane stages 16 bytes of each shared copy's row (Metal: 4
  values): 4096^2, us, f16 M = 32 99 -> 82 (cuBLAS 77), M = 512 409 -> 322
  (295); q4_0:f16 M = 32 185 -> 110. Asynchronous copies
  (`GPUSharedAsync`, cp.async) without double buffering, and other block
  shapes (see `cuda_shapes()`), were slower.
- CUDA rows for every blocked format GGML's CUDA backend runs (q8 acts and
  `:f16`). q2_0's integer dot tripped a Deinterleave internal error (a
  ramp % 2 inside a vector reduction to fewer lanes; fixed in core). 4096^2,
  us, best Halide row (q8 acts or `:f16`) vs GGML (f32 acts; mmvq at M <=
  8, mmq above), GGML / Halide:
  M = 1: 0.80 (q3_K, 42.3 vs 33.9) to 1.52 (q4_0, 23.1 vs 35.0); below 1
  only q5_K 0.92, q3_K 0.80.
  M = 8 (q8 acts): 0.52 (q5_K, 117 vs 61) to 1.39 (q3_K); q4_1, q5_1,
  q4_K, q5_K (two-scale formats) near 0.5-0.6.
  M = 32 (`:f16`): 0.43 (q4_K, 134 vs 58) to 0.85 (q2_K).
  M = 512 (`:f16`): 0.61 (q5_0) to 1.45 (q2_K, 409 vs 595).
  mmq is int8 MMA on q8_1 acts; ours dequantizes to f16 for wmma.
- CUDA gemm at 32 <= M < 64: a 16 x 32 tile of 1 x 2 warps (was 32 x 32
  of 2 x 2; `GemmShape::wm`, the staging's warps split to match), and
  super-blocks' weights staged 16 values a lane on the one-fragment tiles.
  Core: PTX computes an f16 product of f16 or 8-bit operands in half
  (`mul.rn.f16x2`; registers 124 -> 64, time unchanged). Steps of K of 128
  were no faster overall (and f16's don't unroll). 4096^2, `:f16`, M = 32,
  GGML / Halide: q4_0 0.80 (64.0 vs 79.6), q8_0 0.71, q4_1 0.66, q5_0
  0.63, q5_1 0.63, mxfp4 0.67, iq4_nl 0.78, iq4_xs 0.84, q2_0 0.91,
  q2_K 1.10, q3_K 0.85, q4_K 0.84 (was 0.43), q5_K 0.73, q6_K 0.85; f16
  vs cuBLAS 1.07. M = 16: 0.48 (q5_0) to 0.93 (q2_0); M = 512 unchanged.
- CUDA rows on integer acts: an affine weight's min term summed the act's
  codes per lane byte by byte (unrolled; 128 byte loads beside the dp4a's
  word loads at M = 8). The run is now a vector reduction, which PTX lowers
  to dp4a by ones (core), sharing the codes' loads. (Reading q8_1's s and
  q8_K's bsums instead, derived_sums(), took M = 8 to 72/67us for
  q4_1/q5_1, but M = 1 regressed 1.45x: a second pass over the weights'
  mins.) 4096^2, q8 acts, us, M = 8 (GGML mmvq): q4_1 87 -> 52.9 (47.5),
  q5_1 93 -> 48.3 (50.3), q4_K 99 -> 42.0 (58.0), q5_K 122 -> 56.1 (60.4);
  M = 1 unchanged or faster (q5_K 35.7 -> 33.7).
- CUDA rows on integer acts take 4 rows a warp at every M (was 2 at M > 4;
  8 rows: 0.74-1.07x of 4). 4096^2, q8 acts, M = 8, us (GGML mmvq):
  q4_0 57.8 -> 42.5 (49.9), q8_0 67.3 -> 54.4 (55.7), q5_0 67.7 -> 50.9
  (52.2), iq4_nl 62.9 -> 47.3 (51.6), q4_K 42.0 -> 38.9 (59.2). GGML /
  Halide at M = 8 now 1.02 (q8_0) to 1.96 (q2_K); at M = 1 1.05 (q6_K) to
  1.89 (q2_K) but q3_K 0.90 (43.1 vs 38.9).
- apps/ggml: CUDA gemm on blocked weights at M >= 256 in 64 x 128 tiles of
  2 x 2 warps (was 128 x 64 of 4 x 1): each weight decoded once per 128
  act rows, not 64. 4096^2, f16 acts, us: M = 512 q6_K 669 -> 515, q5_0
  561 -> 460, mxfp4 569 -> 469, q3_K 602 -> 444; f16 unchanged (flat). At
  M = 64..128 mixed (0.7-1.2x), so not there. GGML / Halide at M = 512
  0.61-0.84 -> 0.74-0.98 (q2_K 1.44); M = 256 0.77-1.0 (q2_K 1.6).
