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
  (Removed after the AMX repack: M = 32 now runs `blocked_cols()`, and at
  M = 4..7, the knob's only reach, q6_K is 1.1x faster wide.)
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
  `vinserti64x4`, AVX-512 having no zmm `vpsignb`). The cols gemm always folds,
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
  (zmm `vpmovsxwd` + `vpaddd`) inside the ymm gemv loop; on the test machine a zmm uop
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
- Tried, not kept: split K for the gemm at M = 16..31 (latency bound:
  N / 64 x M / 16 tiles). A clone of dot rfactored over chunks of K, the
  partials in their own kernel (gpu_staged on the fragments), summed per
  output. q4_0 at M = 16: 80.9 -> 56.5us at 1024 values a chunk (GGML
  mmq ~52; 512: 57.6, 2048: 61.6), but slower at M = 32 (79.6 -> 84.9).
  rfactor's tail guard (`r.y` clamped to the blocks; the split's predicate
  unprovable for a symbolic K, even under a `K % chunk == 0`
  specialization) gives the staged tiles extents that depend on the block,
  so the 256-value formats fail the wmma partial-overlap check and the
  checked build's bounds go negative. Would need rfactor (or
  specializations) to learn the split is exact.
- Launch overhead (spec item 4). On this machine (WSL2) a CUDA launch costs
  ~11us of host time: an empty kernel launched back to back (runtime API,
  `<<<1, 32>>>`) takes 11.4us a launch to enqueue, 11.5us with the sync
  (two a step: 21.9 / 22.2). The harness times Halide's calls back to back
  (async, one sync per sample), so a call costs max(host, GPU). q4_0, us a
  call, Halide f16 acts / f32 acts, GGML a node (its graph of 16 nodes,
  each an act quantize and a mmvq or mmq launch): 32x32x1 9.9 / 11.5, 30.1;
  256x256x1 10.1 / 10.3, 34.1; 1024x1024x1 13.8 / 12.0, 39.5; 32x32x32
  10.4 / 10.5, 38.3; 256x256x32 9.9 / 10.7, 39.5. So Halide's per-call
  overhead is one launch's (the pipeline's own host work, under the
  launch, is not visible), GGML's ~3 (~30us); at 4096^2 every case's
  kernel takes longer than a launch (M = 1: 20-45us), so the timings above
  are kernel bound.
- CUDA gemm at M = 64..128 on blocked weights (f16: M = 64..79) in 32 x 64
  tiles of 2 x 2 warps (was 128 x 64 of 4 x 1): 4x the blocks (M = 64:
  128 for 46 SMs, was 32). 4096^2, f16 acts, us: M = 64 q6_K 204 -> 111,
  iq4_xs 190 -> 103, q3_K 153 -> 102, q4_0 175 -> 132, f16 117 -> 93;
  M = 128 q6_K 232 -> 185, q5_0 217 -> 172. GGML (mmq) / Halide at M = 64
  0.47-0.75 -> 0.57-0.89 (q2_K 1.04), at M = 128 0.55-0.82 -> 0.60-0.90
  (q2_K 1.57). (16 x 64 of 1 x 2 warps: as good at M = 64, 2x slower at
  96; 32 x 32 of 2 x 2: 1.05-1.2x slower.)
- CUDA gemm k step (values staged a step, 64) not changed. 128: q5_0 at
  M = 32 100 -> 82us, iq4_xs and q4_0 slightly faster, but q6_K and q4_K
  slower at M = 16..128 (q6_K at M = 128 190 -> 317us), and f16 fails to
  compile (the staging loop's extent is no longer constant). 32: slower
  everywhere but q4_K at M = 16 (86 -> 72us).
- CUDA f16 weights at M = 2..15 on the wmma gemm (one 32 x 16 tile of 2
  warps, guarded; was the rows), at any M * N: 4096^2 x 8 152 -> 76us
  (cuBLAS 79, GGML's mmf on f32 acts 70), x 4 105 -> 75, 4096 x 2048 x 8
  80 -> 51 (cuBLAS 46). One warp's 16 x 16 1.05x slower; at M = 1 the same
  as the rows (77 vs 78us), which stay.

## Phase 2 report (2a CUDA gemm, 2b CUDA GEMV)

RTX A4500 (sm_86, 46 SMs), driver 596.71, CUDA 13.2, WSL2; at b67e43bfb
(f16 at M = 8 after it, the rest at 738327646: same kernels). K = N =
4096; t_GGML / t_ours, best Halide path (q8 `dot` or f16 acts) against
GGML's best (mmvq/mmq/mmf on f32 acts, cuBLAS); `--early-stop`, cases
under 0.97 re-run twice, minimum kept (first runs: M = 1 iq4_nl/iq4_xs/
mxfp4/q2_0 0.5-0.92, from GPU clock drops while CPU providers interleaved).

| M | match (>= 0.95) | median | min | max |
|---|---|---|---|---|
| 1 | 12/15 | 1.08 | 0.83 (q3_K) | 1.96 |
| 8 | 14/15 | 1.08 | 0.92 (f16) | 1.93 |
| 16 | 1/15 | 0.61 | 0.47 | 1.08 |
| 32 | 2/15 | 0.70 | 0.56 | 1.07 |
| 64 | 1/15 | 0.63 | 0.52 | 0.97 |
| 128 | 1/15 | 0.70 | 0.54 | 1.31 |
| 256 | 1/15 | 0.81 | 0.71 | 1.41 |
| 512 | 1/15 | 0.78 | 0.71 | 1.35 |

- Everything runs; `--check` passes on all 348 smoke cases (dot, f32,
  f16 acts) and on the odd shapes above.
- Sloc: schedule.h +471 / -83 since fecbc0001; core +236 / -1 (3
  commits: Deinterleave across VectorReduce, test deinterleave_vector_reduce;
  PTX f16 products, test cuda_f16_product; PTX dp4a sums, test
  cuda_8_bit_sum). No ExtractWMMAOperations change (no int8 MMA).
- Gaps, M >= 16:
  1. f16 decode-to-shared vs GGML's int8 mma on q8_1 (mmq): q4_0's kernel
     decodes in registers (`mul.rn.f16x2` by the scale) and `st.shared` 63x
     per 120 `wmma.mma ... f32.f32` (m16n16k16); mmq's int8 mma does twice
     the MACs per instruction and stages half the bytes.
  2. No pipelining: no `cp.async`, one stage (`bar.sync` 20, loads then
     mma), 56-128 registers a thread; at M <= 128 the grid (32-128 blocks)
     doesn't fill 46 SMs' latency. Split-K at M = 16 (80.9 -> 56.5us, GGML
     52) blocked by rfactor's tail guard (above).
  3. Table formats (iq4_nl, iq4_xs, mxfp4) 0.5-0.75: their luts in shared
     memory per block, a lookup per value against q4_0's arithmetic decode.

## Phase 1 follow-up: CPU gaps (after the Phase 2 report)

CPU run at `63cd29ebb` (GGML / Halide, the fastest path each side, 4096^2
unless noted; geomean per format): the widest gaps were q4_1 everywhere
(0.30-0.61: GGML's AMX buffer, our AMX gemm refused affine weights), M = 8
(q4_0 0.51, q4_1 0.48, q8_0 0.79 on 12 threads: GGML's AMX runs partial
tiles, ours started at M = 16), and M = 512 on the 256-value formats (0.5-0.7
against GGML's tiled gemm, which decodes a weight panel once for all of M).

Core:

- `ExtractTileOperations`: each Tile allocation's sub-tiles and shape were
  kept across allocations, so a pipeline with two tile shapes (two
  specializations' accumulators: 16 x 16 and 16 x 8) failed ("Access to AMX
  tile has different size and strides"). Now scoped per allocation.

App changes:

- `blocked_amx()` takes affine weights (q4_1, q5_1): their min term split off
  by `derived_sums()` (min times the act's `s` port), one f32 product per block
  and output alongside the tile ops, as GGML's AMX `acc_C`. 4096^2, f32 acts,
  ms (GGML's AMX): q4_1 M = 32 one thread 12.7 -> 4.65 (3.81); twelve threads
  M = 32 1.39 -> 0.52 (0.64), M = 512 22.3 -> 8.2 (9.9). q5_1 (no GGML AMX
  kernel) M = 32 one thread 15.1 -> 6.4, twelve threads 1.80 -> 0.70, M = 512
  30.5 -> 9.3.
- `blocked_amx()` on 8-column tiles (amx8: 8 rows of the A tile) for 8 <= M <
  16. 4096^2, M = 8, twelve threads, us (GGML's AMX): q4_0 415 -> 177 (165),
  q4_1 391 -> 192 (186), q8_0 356 -> 146 (160); one thread 0.70-0.88 of
  GGML.
- `blocked_cols()` macro tiles for M >= 256 (64 rows x 256 columns), after
  GGML's tiled gemm (256 x 256 windows, its accumulator in L2): per 256-value
  block, the 64 rows' weights decoded once, then swept by 8-row x 16-column
  micro tiles across all 256 columns (was: 8 x 32 tiles, each block decoded
  per 32 columns). The macro's own loops are scheduled under its
  specialization: `s`, its acc and the sums are hoisted out of dot, so their
  unspecialized stages are every branch's (an 8-row split there over-read
  `w` on the 2-row gemv tiles). 4096^2, M = 512, twelve threads, ms (GGML
  tiled; ratio before -> after): q3_K 12.9 -> 9.8 (6.8; 0.54 -> 0.69), q4_K
  9.0 -> 8.4 (6.2; 0.66 -> 0.74), q5_K 9.6 -> 8.1 (6.3; 0.59 -> 0.78), q6_K
  11.2 -> 8.4 (7.3; 0.68 -> 0.87), q2_K 10.5 -> 8.8 (8.3; 0.78 -> 0.94),
  iq4_xs 11.2 -> 8.4 (5.4; 0.50 -> 0.64), iq3_s 13.6 -> 8.2 (6.4; 0.49 ->
  0.78), iq2_s 13.9 -> 8.5 (7.3; 0.50 -> 0.86), iq1_s 11.5 -> 7.3 (6.0; 0.51
  -> 0.81). M = 32 and 128 unchanged. A 64 x 128 macro (for M = 128 too) was
  slower at both M = 128 and 512 (iq4_xs 512: 13 ms).
- `offset_codes()` hands later tiles the pair an earlier tile's call made, so
  every `blocked_cols()` tile stages the unsigned codes (only the first did).
- `q4_1.4x8`: a layout row for q4_1 (its definition unchanged: four rows'
  records interleaved in 8-byte pieces, as `q4_0.i4.4x8`), for GGML's AMX
  q4_1 gemv's M = 1. GGML has no q4_1 repack; the harness's own
  `make_block_q4_1x4` lays out the oracle's records. `blocked_rows()` takes
  affine weights without sub-blocks: the min term from the act's `s` port
  (`derived_sums()`, as `blocked_amx()`). 4096^2, M = 1, us (GGML's AMX
  gemv): one thread 641 -> 402 (393), twelve threads 82 -> 48.4 (44.0).
- `blocked_cols()` on 8-column tiles (`cols8`, 8 rows x 8 columns) for 8 <= M
  < 16: M = 8 fell to the generic gemm tile, slower than M = 16. 4096^2, M =
  8, twelve threads, us, best path each side (GGML's; ratio before -> after):
  iq4_xs 376 -> 262 (185 AMX; 0.53 -> 0.71), q5_K 366 -> 269 (206 AMX; 0.55
  -> 0.77), q4_K 298 -> 264 (214 AMX; 0.65 -> 0.81), q6_K 461 -> 293 (383 AMX;
  0.85 -> 1.31), q3_K 548 -> 385 (473; 0.90 -> 1.23), q2_K 300 -> 277 (245
  repack; 0.86 -> 0.88); the iq2/iq3/iq1 formats 1.14-1.67, tq1_0 0.88,
  tq2_0 2.55. 16 and 32 rows per tile were slower (303-340). M = 16 and 32
  were already 0.97-1.51 of GGML after the macro tiles. The remaining M = 8
  gaps are against GGML's AMX kernels for q4_K, q5_K and iq4_xs: AMX with
  sub-block scales (`amx_ok` excludes `fmt.sub`).
- `blocked_amx()` with sub-block scales, as GGML's AMX K-quant kernels: one
  tile op per sub-block (rfactor across the sub-blocks), its int32 result
  times the sub-block's scale; the mins' term across the columns, as
  `blocked_cols()`. Steps at q4_K, M = 8, twelve threads, us: first cut 707
  (byte gathers of the scales, sums vectorized across n) -> 254 (sums across
  m, rows unrolled: the act's sub-block sums CSE) -> 238 (the scale decoders
  cloned per consumer, a row's sub-blocks per vector) -> 232 (the act's bsums
  staged, columns innermost). Interleaved A/B against `cols8`, 4096^2, M = 8,
  ratio AMX / cols8: 32-value sub-blocks iq1_s, iq4_xs, q4_K 0.81, q5_K 0.90,
  iq2_xxs, iq3_s 0.97, iq3_xxs 0.98; 16-value ones q3_K 1.08, iq2_s 1.06,
  q6_K 1.17, iq2_xs 1.19, q2_K 1.31, iq1_m 1.34 (twice the tile ops and
  rescales). So the 8 <= M < 16 tile takes AMX when `fmt.sub >= 32`. Against
  GGML, us (GGML's best; ratio after cols8 -> now): q4_K 229 (226 AMX; 0.81
  -> 0.99), q5_K 223 (202 AMX; 0.77 -> 0.90), iq4_xs 204 (187 AMX; 0.71 ->
  0.92), iq1_s 300 (493; 1.64), iq2_xxs 300 (436; 1.45), iq3_s 470 (538;
  1.15), iq3_xxs 403 (460; 1.14). The rest is structural: the weight operand transposed per
  sub-block at run time (GGML repacks offline), the act staged per sub-block.

## CPU sweep (`adfc2a804` + harness `a57a4b4d7`)

Raw data and scripts: `doc/data/` (`cpu-sweep/`, `table.py`, `analyze.py`).
4096^2 weights, `--early-stop`, every CPU provider, Windows power plan High
Performance (earlier runs in this log were under Balanced: their absolute
times are lower clocks', both sides alike). GGML / Halide, each side's
fastest path (GGML's activations f32; > 1: Halide faster). f32 weights: no
Halide CPU kernel. 16 runs, 0 check failures.

Twelve threads:

| format | M=1 | M=8 | M=16 | M=32 | M=64 | M=128 | M=256 | M=512 |
|---|---|---|---|---|---|---|---|---|
| bf16 | 1.10 | 1.29 | 1.19 | 1.28 | 1.40 | 1.59 | 1.51 | 1.46 |
| f16 | 1.16 | 1.12 | 1.16 | 1.30 | 1.48 | 1.25 | 1.30 | 1.26 |
| f32 | - | - | - | - | - | - | - | - |
| iq1_m | 0.78 | 1.41 | 1.97 | 1.84 | 1.12 | 1.28 | 1.00 | 0.95 |
| iq1_s | 0.86 | 1.68 | 1.46 | 1.22 | 0.97 | 0.96 | 0.93 | 0.88 |
| iq2_s | 0.82 | 1.22 | 1.20 | 0.97 | 0.85 | 0.80 | 0.95 | 0.88 |
| iq2_xs | 0.82 | 1.55 | 1.63 | 1.15 | 0.98 | 0.93 | 0.91 | 0.86 |
| iq2_xxs | 0.80 | 1.34 | 1.38 | 1.23 | 1.03 | 1.03 | 0.92 | 0.89 |
| iq3_s | 0.97 | 1.05 | 1.27 | 1.11 | 0.97 | 0.97 | 0.86 | 0.92 |
| iq3_xxs | 1.06 | 1.03 | 1.30 | 1.25 | 0.97 | 0.90 | 0.88 | 0.97 |
| iq4_nl | 1.08 | 1.89 | 2.17 | 2.25 | 2.15 | 2.36 | 2.48 | 2.23 |
| iq4_xs | 0.83 | 0.82 | 0.95 | 0.89 | 0.86 | 0.90 | 0.78 | 0.81 |
| mxfp4 | 1.11 | 2.04 | 2.11 | 2.57 | 2.18 | 2.35 | 2.35 | 2.27 |
| nvfp4 | 0.83 | 1.73 | 1.78 | 2.00 | 2.14 | 2.05 | 2.29 | 2.19 |
| q1_0 | 0.99 | 1.04 | 1.15 | 1.28 | 1.31 | 1.39 | 1.39 | 1.43 |
| q2_0 | 6.57 | 10.31 | 11.98 | 12.72 | 12.76 | 14.63 | 12.83 | 13.05 |
| q2_K | 0.81 | 0.91 | 1.47 | 1.38 | 1.18 | 1.32 | 1.16 | 1.13 |
| q3_K | 0.69 | 1.05 | 1.07 | 0.96 | 0.85 | 0.76 | 0.75 | 0.82 |
| q4_0 | 0.68 | 0.89 | 1.20 | 1.23 | 1.41 | 1.40 | 1.37 | 1.38 |
| q4_1 | 0.76 | 0.95 | 1.15 | 1.25 | 1.21 | 1.27 | 1.21 | 1.33 |
| q4_K | 0.85 | 0.79 | 0.98 | 1.02 | 1.00 | 0.94 | 0.77 | 0.81 |
| q5_0 | 0.87 | 2.55 | 4.61 | 4.23 | 6.03 | 5.90 | 7.05 | 7.38 |
| q5_1 | 0.88 | 2.34 | 3.53 | 3.84 | 4.62 | 5.75 | 5.39 | 5.53 |
| q5_K | 1.34 | 0.83 | 0.98 | 1.06 | 0.99 | 1.01 | 0.86 | 0.96 |
| q6_K | 0.83 | 1.12 | 1.58 | 1.10 | 1.16 | 1.13 | 0.90 | 0.98 |
| q8_0 | 1.67 | 1.07 | 1.43 | 1.35 | 1.32 | 1.33 | 1.32 | 1.31 |
| tq1_0 | 1.02 | 0.86 | 1.83 | 1.79 | 5.72 | 5.93 | 6.64 | 6.29 |
| tq2_0 | 0.92 | 2.26 | 3.35 | 3.33 | 5.78 | 5.83 | 6.13 | 5.48 |
| **>= 0.95** | 11/27 | 20/27 | 27/27 | 26/27 | 24/27 | 21/27 | 16/27 | 19/27 |
| **median** | 0.87 | 1.12 | 1.43 | 1.28 | 1.21 | 1.28 | 1.21 | 1.26 |
| **geomean** | 0.99 | 1.36 | 1.66 | 1.61 | 1.64 | 1.67 | 1.60 | 1.61 |

One thread:

| format | M=1 | M=8 | M=16 | M=32 | M=64 | M=128 | M=256 | M=512 |
|---|---|---|---|---|---|---|---|---|
| bf16 | 1.09 | 1.47 | 1.37 | 1.51 | 1.63 | 1.56 | 1.55 | 1.58 |
| f16 | 1.00 | 0.94 | 0.98 | 1.04 | 1.09 | 1.10 | 1.11 | 1.08 |
| f32 | - | - | - | - | - | - | - | - |
| iq1_m | 0.78 | 1.67 | 1.69 | 1.27 | 0.95 | 0.75 | 0.77 | 0.75 |
| iq1_s | 0.85 | 1.41 | 1.35 | 1.14 | 0.86 | 0.71 | 0.77 | 0.71 |
| iq2_s | 0.81 | 1.07 | 1.12 | 0.90 | 0.65 | 0.55 | 0.76 | 0.71 |
| iq2_xs | 0.81 | 1.32 | 1.38 | 1.09 | 0.84 | 0.71 | 0.74 | 0.71 |
| iq2_xxs | 0.79 | 1.34 | 1.36 | 1.15 | 0.88 | 0.77 | 0.74 | 0.70 |
| iq3_s | 1.04 | 0.97 | 1.13 | 1.02 | 0.75 | 0.62 | 0.73 | 0.69 |
| iq3_xxs | 0.91 | 0.92 | 1.17 | 1.06 | 0.80 | 0.67 | 0.73 | 0.70 |
| iq4_nl | 1.16 | 1.72 | 2.24 | 2.25 | 2.13 | 2.09 | 2.12 | 2.10 |
| iq4_xs | 0.62 | 0.81 | 0.86 | 0.74 | 0.70 | 0.71 | 0.72 | 0.67 |
| mxfp4 | 1.18 | 2.04 | 2.30 | 2.32 | 2.17 | 2.11 | 2.13 | 2.12 |
| nvfp4 | 0.90 | 1.85 | 1.86 | 1.89 | 1.89 | 1.88 | 1.84 | 1.82 |
| q1_0 | 1.08 | 1.30 | 1.29 | 1.30 | 1.30 | 1.31 | 1.31 | 1.29 |
| q2_0 | 6.85 | 11.19 | 11.16 | 11.34 | 10.89 | 11.47 | 11.34 | 11.32 |
| q2_K | 0.80 | 0.92 | 1.26 | 1.20 | 1.10 | 0.97 | 0.93 | 0.89 |
| q3_K | 0.78 | 1.11 | 1.07 | 0.87 | 0.69 | 0.61 | 0.70 | 0.67 |
| q4_0 | 0.79 | 0.69 | 1.06 | 0.87 | 0.97 | 0.98 | 0.98 | 0.98 |
| q4_1 | 0.89 | 0.73 | 0.95 | 0.80 | 0.79 | 0.79 | 0.78 | 0.80 |
| q4_K | 0.92 | 0.65 | 0.84 | 0.77 | 0.76 | 0.74 | 0.80 | 0.71 |
| q5_0 | 0.91 | 2.55 | 4.35 | 4.33 | 5.81 | 5.80 | 5.83 | 5.75 |
| q5_1 | 0.92 | 2.32 | 3.69 | 3.73 | 4.67 | 4.69 | 4.84 | 4.69 |
| q5_K | 1.14 | 0.78 | 0.95 | 0.86 | 0.81 | 0.81 | 0.78 | 0.73 |
| q6_K | 0.98 | 1.20 | 1.40 | 1.19 | 0.95 | 0.81 | 0.81 | 0.76 |
| q8_0 | 1.17 | 0.84 | 0.97 | 0.95 | 0.93 | 0.91 | 0.94 | 0.94 |
| tq1_0 | 1.07 | 0.94 | 1.72 | 1.71 | 5.57 | 5.48 | 5.43 | 5.26 |
| tq2_0 | 1.11 | 2.75 | 4.15 | 4.37 | 6.16 | 6.23 | 6.06 | 5.32 |
| **>= 0.95** | 12/27 | 17/27 | 24/27 | 20/27 | 13/27 | 13/27 | 12/27 | 12/27 |
| **median** | 0.92 | 1.20 | 1.35 | 1.15 | 0.95 | 0.91 | 0.93 | 0.89 |
| **geomean** | 1.00 | 1.32 | 1.56 | 1.44 | 1.41 | 1.32 | 1.37 | 1.31 |

`analyze.py --by-m` (held-out layout; matched = G/H >= 0.95): twelve threads,
blocked-dot 40% (M = 1), 91-98% (M = 8..32), 50-76% (M >= 64);
format-specific (GGML's AMX/repack paths) 43-46% (M <= 8), 78-100% above.
One thread: blocked-dot 49% (M = 1), 89-100% (M = 8..32), 48-53% (M >= 64);
format-specific 27-60%.

Where we trail:

- M = 1 (0.62-0.92 for most quantized formats): at 4096^2 the weights fit
  in L3 (30 MB), so it is a 30-80 us cache-resident kernel; GGML's AMX gemv
  (q4_0 32 us vs our 45) and its lower per-call overhead win; CIs are
  10-20%.
- One thread, M >= 64, the 256-value K/iq formats (0.55-0.82): GGML's tiled
  VNNI gemm against our `blocked_cols()`; on twelve threads the same formats
  are 0.75-1.03.
- iq4_xs, q4_K, q5_K at M = 8 (0.65-0.83): GGML's AMX K-quant kernels with
  offline-repacked weights; ours transposes per sub-block (see above).
- q4_1, q4_0, q8_0 one thread at M >= 8 (0.69-0.98).

Large leads (q2_0, q5_0/q5_1, tq1_0/tq2_0, iq4_nl, mxfp4, nvfp4: 2-14x) are
formats GGML runs only through its generic blocked vec_dot.

## AMX repack (16x4)

New layouts `q4_K.16x4`, `q5_K.16x4`, `iq4_xs.16x4` (`formats.cmake`; layout
rows of existing formats, not definition changes): `Interleave{16, 4}` puts 16
rows' 4-byte code pieces adjacent, which is AMX's B tile (as GGML's AMX buffer
prepacks). The harness's `ggml_repack` transcribes it; `kquants.h`'s
aos/soa-only guard on q4_K/q5_K is lifted.

Schedule (`blocked_amx(..., packed = true)`, chosen by generic criteria:
integer, AVX-512 + AMX, 16x4 record, one row per piece, block % 64 == 0):

- Codes decode in place per block at `(acc, ry)`: one dense 64-byte load and a
  nibble mask per tile row, storage `(ki, row, ko, b)`; no per-sub-block
  staging/transpose of `w`. Shared by every sub-block's `tdpbssd` and every
  16-row M sub-tile (q4_K M = 256: 4605 -> 3475 us).
- New tile `amx2` (32 M columns, two 16-row subtiles per decode) for M >= 32.
- Routing: amx4 (M >= 64), amx2 (>= 32), amx (>= 16), amx8 only at M = 8
  (two overlapping ShiftInwards 8-row tiles double the cost at M = 9..15);
  everything else runs the record gemv (`blocked_rows`), which beats GGML's
  AMX at M <= 4.
- iq4_xs: with `packed` the signed codebook is no longer biased to unsigned
  (`derived_sums` fold) since `tdpbssd` is signed x signed: 1.1-1.3x. The aos
  amx8 path keeps the bias (1.06x faster there).
- Tried: a guarded 16-row tile for 9 <= M < 16 (`guard_m`); fails with AMX
  tile size/stride mismatch and bounds conflicts across the specialized
  rfactor Funcs. Reverted.

Twelve threads, 4096^2, best of 2 (us; `data/amx-repack/`, `rp.sh`), GGML best
of its providers vs ours (aos, 16x4); G/H for 16x4:

| fmt | M | GGML | aos | 16x4 | G/H |
|---|---|---|---|---|---|
| q4_K | 1 | 42 | 93 | 31 | 1.36 |
| q4_K | 4 | 134 | 185 | 89 | 1.50 |
| q4_K | 8 | 165 | 225 | 182 | 0.91 |
| q4_K | 12 | 194 | 435 | 241 | 0.81 |
| q4_K | 16 | 224 | 249 | 289 | 0.77 |
| q4_K | 32 | 433 | 448 | 479 | 0.90 |
| q4_K | 64 | 844 | 896 | 912 | 0.93 |
| q4_K | 256 | 3133 | 3880 | 3287 | 0.95 |
| q5_K | 1 | 61 | 101 | 39 | 1.57 |
| q5_K | 4 | 161 | 201 | 123 | 1.32 |
| q5_K | 8 | 187 | 234 | 197 | 0.95 |
| q5_K | 12 | 224 | 448 | 331 | 0.68 |
| q5_K | 16 | 255 | 235 | 281 | 0.91 |
| q5_K | 32 | 473 | 482 | 469 | 1.01 |
| q5_K | 64 | 911 | 907 | 877 | 1.04 |
| q5_K | 256 | 3189 | 3894 | 3436 | 0.93 |
| iq4_xs | 1 | 53 | 73 | 42 | 1.27 |
| iq4_xs | 4 | 139 | 187 | 124 | 1.12 |
| iq4_xs | 8 | 165 | 212 | 146 | 1.13 |
| iq4_xs | 12 | 196 | 421 | 397 | 0.49 |
| iq4_xs | 16 | 230 | 231 | 205 | 1.13 |
| iq4_xs | 32 | 394 | 458 | 362 | 1.09 |
| iq4_xs | 64 | 768 | 860 | 770 | 1.00 |
| iq4_xs | 256 | 2796 | 3819 | 2874 | 0.97 |

16x4 leads at M <= 4 (1.1-1.6x) and for iq4_xs at M <= 64; q5_K is at
parity at M = 32/64 (1.01-1.04). Versus aos it is 1.0-1.3x faster except
q4_K/q5_K at M = 16. Remaining gaps: M = 9..15 (rows path, 0.5-0.8),
q4_K/q5_K at M = 16 (0.77/0.91: one 16x16 accumulator's serial
zero/load/dpbssd/store chain per sub-block; GGML pairs two B tiles per A),
q4_K at M >= 8 (0.90-0.95) and q5_K at M = 8/256 (0.93-0.95).

## Self-review cleanup

- `gemm_wide_subs` and its two `[x86]` q6_K rows removed. The `q6_K.8x8`
  row was dead (identical stmt either way: that layout's gemm on x86 is the
  record gemv); the `q6_K` row reached only M = 4..7 (the generic gemm
  tiles; M >= 8 is `blocked_cols()`), where it now cost 9-13%. q6_K, 4096^2,
  best of 3, us, row / default: one thread M = 4 2284 / 2075, M = 5..7
  4440-4566 / 4051-4102, M = 32 4403 / 4413; twelve threads M = 4 245 /
  213, M = 5..7 459-468 / 409-422, M = 32 485 / 462.
- `column_sums()`: the column-vectorized code-free sums, duplicated in
  `blocked_cols()` and `blocked_amx()`, are one helper. Stmts of q4_K, q5_K,
  q6_K, q2_K, q4_K.16x4, iq4_xs.16x4 mul_mat identical (up to generated
  temporaries' numbering).

## x86 gap closure: sub-block scales in the lanes (vec_dot, M = 1)

Baseline (`23ce3ecee`, partial paper sweep in `data/paper-sweep/base/`:
vec_dot, and mul_mat M = 1 at 4096^2 and 4096x1024, T1 and T12): vec_dot
held-out 54 cases, 38.9% matched, median 0.868.

Diagnosis (llvm-mca plus measured slopes): the sub-block formats' inner
loops. Ours did a `vpdpbusd` per sub-block, then a `vpmulld` by its
scale, plus abs/sign fixups for signed codes; GGML's AVX2 path does
`pmaddubsw` (|codes| x signed acts) then `pmaddwd` by the int16 scales.

Core (each with a test):
- `ef3231eef` Deinterleave: a `dynamic_shuffle`'s table is not
  lane-parallel with its result (only the indices are deinterleaved; other
  calls with a vector argument of a different lane count are shuffled).
  It crashed iq4_xs (`Shuffle vector index out of range`).
  Test: `correctness/deinterleave_dynamic_shuffle`.
- `f3ba8f1b8` CodeGen_X86: the three-factor (`pmaddubsw` + `pmaddwd`)
  dot also takes a fourth sign factor (`psignb`), two signed factors as
  |a| x sign(b, a), factors under `widen_right_mul`, a LUT's bounds (its
  table's), and pair uniformity lane by lane with let-bound constant
  vectors. Test: `correctness/x86_dot_three_factor` (each case fails
  without its piece).
- Arm/Metal: the M3's 982 libraries (all formats' CPU and 164 Metal
  ones, `arm-64-osx-arm_dot_prod-arm_fp16-arm_i8mm[-metal]`, the arm knob
  rows) generated with HEAD's and the new generator and libHalide are
  byte-identical (headers differ only by the output path in the include
  guard). The schedule rule below is VNNI-only.

Schedule (`blocked()`, no new knob): with VNNI, a gemv/vec_dot of signed
codes (not `affine()`: q3_K, the iq formats) keeps its integer sub-block
scales in the lanes at a step of 32, which the new CodeGen_X86 pattern
lowers as GGML does. Affine codes (q4_K, q5_K) keep `vpdpbusd` (with the
scales in the lanes: q4_K 0.91 -> 0.77x, q5_K 1.15 -> 0.88x).

One-thread vec_dot, G/H, 30 rounds (K = 4096 / 14336):

| type | before | after |
|---|---|---|
| iq4_xs | 0.99 / 1.01 | 1.17 / 1.21 |
| iq2_xs | 0.84 / 0.82 | 0.98 / 0.98 |
| iq2_s | 0.81 / 0.83 | 0.90 / 0.89 |
| q3_K | 0.72 / 0.74 | 0.79 / 0.80 |
| iq3_xxs | 0.84 / 0.86 | 0.86 / 0.88 |
| iq1_s | 0.84 / 0.84 | 0.86 / 0.87 |
| iq1_m | 0.74 / 0.75 | 0.75 / 0.78 |
| iq2_xxs | 0.81 / 0.82 | 0.84 / 0.83 |
| iq3_s | 1.00 / 1.07 | 1.06 / 1.11 |
| q4_K, q5_K | unchanged (noise) | |

mul_mat 4096^2 M = 1, G/H (fastest of each side), T1 / T12:

| type | before | after |
|---|---|---|
| iq1_m | 0.77 / 0.75 | 0.82 / 0.81 |
| iq1_s | 0.85 / 0.79 | 0.94 / 0.94 |
| iq2_xs | 0.81 / 0.77 | 0.94 / 0.90 |
| iq2_xxs | 0.80 / 0.76 | 0.87 / 0.86 |
| iq3_xxs | 0.89 / 0.81 | 0.90 / 0.92 |
| iq3_s | 1.03 / 0.94 | 1.08 / 1.12 |
| q3_K | 0.80 / 0.70 | 0.83 / 0.81 |
| iq2_s | 0.82 / 0.80 | 0.85 / 0.79 (with the knob below) |

`[x86] iq2_s mul_mat` row gains `interleave=1`: with the scales in the
lanes its two-row gemv regressed (M = 1, one thread, 1439 -> 1225 us with
one row; M = 8/32 and the f16/f32 acts within 2%). The other formats'
gemvs are within 1-4% either way with one row, so no rule.

Checks: `--check` vec_dot 484, codec 714, mul_mat 1572 results, 0
failures; Halide `correctness_{deinterleave*,x86_dot*,vector_reductions,
mixed_sign_dot_product,...}` (14) and `simd_op_check_x86` pass.

### 16x4 records for 32-value blocks (q4_0, q4_1, q8_0)

GGML's q4_0 gemv at M = 1 (its AMX backend, 275 us at 4096² T1) beat ours
(i4.4x8, 343 us): it packs 16 rows in 4-byte pieces (lanes are rows, the act
broadcast). Our 16x4 layout (`Interleave{16, 4}`) already does this for q4_K,
q5_K and iq4_xs; three new layout rows add it for q4_0, q4_1 and q8_0 (the
harness's `interleave_fields` repack; no kernel code). Two generic schedule
fixes it needed:

- `tile_out`: a tile wider than 16 in n took the macro-tile path, which splits
  m by 8; for a one-row gemv tile (the two-record `blocked_rows` tile, 32 rows)
  that read output row -7 when M < 8 (`--check` caught it on the checked
  kernel). The macro path now needs mt >= 8.
- the two-record tile's task size (`rows`) is at least its `nt` (16 < 32
  split n by 0).
- `amx_recs` admits blocks of up to 64 values (as `amx_ok`): one tile op per
  32-value block, so M >= 8 runs AMX on the records.

G/H (GGML / ours), 4096², fastest of each side; before -> after (16x4):

| | M=1 T1 | M=8 T1 | M=32 T1 | M=1 T12 | M=8 T12 | M=32 T12 | M=512 T12 |
|---|---|---|---|---|---|---|---|
| q4_0 | 0.79 -> 0.97 | 0.68 -> 1.00 | 0.83 -> 1.12 | 0.68 -> 1.02 | 0.85 -> 1.23 | 1.23 -> 1.43 | 1.33 -> 1.52 |
| q4_1 | 0.89 -> 0.94 | 0.75 -> 0.83 | 0.81 -> 0.89 | 0.68 -> 1.02 | 0.88 -> 1.03 | 1.20 -> 1.29 | 1.12 -> 1.27 |
| q8_0 | 1.16 (qsi8cx) | 0.85 -> 0.92 | 0.95 -> 1.01 | 1.71 (qsi8cx) | 1.04 -> 1.17 | 1.26 -> 1.33 | 1.22 -> 1.27 |

q4_0 M=1 T1 4096x1024: 0.64 -> 0.88; T12: 1.21. `--check` mul_mat and codec,
q4_0/q4_1/q8_0: 0 failures. M3 replay (982 Arm/Metal objects, HEAD vs this
schedule.h): all byte-identical.

### blocked_cols: staged scales, strip order, vectorized sums combine

GGML's tiled VNNI gemm (8 rows x 16 columns per step, per sub-block a
`vpdpbusd` run then a `vpmulld` by the scale) beat our `blocked_cols` at
M >= 32 for the 256-value K-quant and iq formats (T1 4096x1024 M=512: 0.62 -
0.78). Three generic changes to `blocked_cols` (x86 only: `cols_ok`):

- sub-block scales decoded per block, vectorized across sub-blocks (a clone
  computed at the block loop), not per row and column group inside the
  sub-block loop (scalar code with spills before; now a broadcast load).
  q4_K M=512 T1 16.3 -> 15.0 ms, q3_K 20.0 -> 18.2.
- a macro tile's 8-row strips inside its 16-column groups (each block's
  decoded weights reused across the columns from L1). q4_K 15.0 -> 14.6.
- the derived sums' difference (dot += with - sums) vectorized across the
  columns under the tile's condition. It was a scalar loop striding 1 KB per
  element, 9% of q4_K's profile. q4_K 14.6 -> 13.4 ms.

G/H after, T1 4096x1024 M=512: q4_K 0.86, q5_K 0.87, q2_K 0.95, q3_K 0.76,
q6_K 0.78, iq2_xxs 0.77. T1 4096² M=32: q4_K 0.82 -> 0.86, q3_K 0.85 -> 0.89,
q5_K 0.87 -> 0.91, iq2_s 0.87 -> 1.08. T12 4096² M=512: q4_K 1.05, q6_K 0.98,
iq2_xxs 0.91, q3_K 0.89. What remains (q3_K profile): the sub-block loop is
63% and matches GGML's instruction mix; the mins/offset sums pass is ~19%
(vpmaddwd with spilled scale broadcasts).

`--check` mul_mat, 15 formats, 5 shapes (M = 8, 33, 37, 300, 512): 0
failures. M3 replay (982 Arm/Metal objects): all byte-identical.

### bf16 gemv by vdpbf16ps; T12 M=1 fixed cost

Priority 2 (T12, M = 1, fixed cost per call): on the smallest shapes ours is
cheaper than GGML's (256x16, T12: 0.2-0.3 us vs 1.7). The T12 4096x1024 gaps
(bf16 0.58, f16 0.73) are not fixed cost: with `--cold`, ours wins (bf16 138
vs 148 us, f16 136 vs 148). Warm, each thread's slice (0.7 MB) is resident in
its own L2, which exposed the kernel's in-L2 throughput: bf16 T1 4096x64
(512 KB) was 12.0 us vs GGML's 8.2. The mul_mat path widened bf16 to f32
(`vpmovzxwd`, `vpslld`, FMA); only vec_dot summed products in pairs for
`vdpbf16ps`. `flat()`'s pairs now apply to both mul_mat tiles too (the rule
`bf16_pairs()`: bf16 weights and acts on an AVX512-BF16 target):

- bf16 T1 4096x64: 12.0 -> 8.95 us (bf16 act), 7.95 (f32 act encoded to bf16)
  vs GGML 8.0.
- bf16 T12 4096x1024: 33 -> 27 us; GGML 19. What remains is T12 alone (ours
  22.3 us at T8, 25.6 at T12; GGML 19.5, 18.6). 8-row tasks instead of 16
  (better balance across 12 threads) were slower at T8 and no faster at T12,
  and the fast-path chunks are in use (584 of 588 calls); pinning one
  thread per core changed nothing. Unexplained.

`--check` mul_mat bf16/f16/f32 (K 4096, 4097, 4100, 512; M 1-300) and
vec_dot bf16/f16: 0 failures. M3 replay (982 objects): all byte-identical
(the rule needs AVX512-BF16).

### q2_K / q6_K vec_dot (diagnosis, no change)

One-thread vec_dot, K = 4096: q2_K 0.76, q6_K 0.80 of GGML. Ours is a zmm
loop (two blocks a step, 68 instructions a block); GGML's is AVX2 ymm (92 a
block). llvm-mca (sapphirerapids) puts ours at 19 cycles a block and GGML's
at 22, but measured ours is 7.4 ns a block and GGML's 5.6. A 256-bit step
(wide = 32 in place of 64, same structure) is slower still for q6_K (168 vs
131 us; q2_K's does not generate), so the zmm width (frequency license) is
not the cause. The one structural difference left: our offset/mins term
(`d.part`, the act's bsums times the sub-block mins) is reduced to a scalar
every block (`vpaddd` shuffles, `vcvtdq2ps`, `vmulss`, `vfmadd231ss`), where
GGML keeps it a vector accumulator across blocks and reduces once. Keeping
it a vector needs `derived_sums()` to rfactor the act sub-blocks before it
hoists the block's float factor; not attempted.

### Paper sweep (final)

`doc/data/paper-sweep/final/` (run.sh, then rerun.sh: 453 cases below 0.97
re-run, minimum kept; `summary.txt` has the tables and every case below
0.95). G/H, held-out layout, matched (>= 0.95) / median:

| op | T | blocked-dot | format-specific |
|---|---|---|---|
| mul_mat (in L3) | 1 | 71.2% / 1.187 | 48.5% / 0.934 |
| mul_mat (in L3) | 12 | 77.4% / 1.367 | 72.6% / 1.128 |
| mul_mat (> L3) | 1 | 79.9% / 1.240 | 63.6% / 1.006 |
| mul_mat (> L3) | 12 | 80.4% / 1.242 | 85.9% / 1.398 |
| vec_dot | 1 | | 38.9% / 0.908 |

By M (in L3 / > L3, blocked-dot median): M = 1 0.94-0.96, M = 8 1.40-1.57,
M = 32 1.26-1.73, M = 512 0.95-1.10. M = 1 and vec_dot remain the gap.
q4_K/q5_K/iq4_xs's held-out layout is 16x4 (it wins M = 1 and T12); at T1
M >= 32 it trails their aos layout's blocked_cols (q4_K 0.66 vs 0.86).

### Vectorized derived_sums accumulator and staged bit-packed sub-block scales

Why `wide = 64` (zmm) lost on Golden Cove (Xeon w5-2455X) even though
`llvm-mca` predicted 19 cycles a block vs GGML's 22: `perf stat` showed
`uops_dispatched.port_1` was 1.4% of cycles (vs 34% in GGML's ymm loop) and
`exe_activity.2_ports_util` was 43%. Any 512-bit instruction shuts off Port 1's
vector ALUs on Golden Cove, leaving only Ports 0 and 5 for all vector work.
Even with `wide = 32`, `d.blk` (`float32 * 16`, two blocks of 8 sub-block
pairs) in `q6_K` `mul_mat` M = 1 was SLP-vectorized by LLVM into one `%zmm`
FMA, which kept Port 1 shut off.

Three generic changes in `schedule.h` (x86):

- `derived_sums()` / `schedule_sums()` (`sum_lanes`: x86 with `ts.nt <= 2`):
  splits the sub-block reduction by 2 (`rxt` extent 2 for `vpmaddwd` /
  `vpdpwssd`) and rfactors `(rtl, r.y)` into `d.blk` before `hoist_invariants()`
  hoists the block's float factor, then folds `d.part` into `d.sum` and `d.blk`
  into `sacc` (`sum_tree` over `rtl`) with `eager_inline()`. The offset/mins
  term accumulates in a `ymm` vector (`sacc`) across blocks and reduces once at
  the end, with zero `%zmm` instructions. In `blocked()`, `gemv` keeps
  `wide = 32, sub = 0` for 16-value sub-blocks (`!gemv` keeps `wide = 64`). In
  `blocked_cols()`, the `macro` (`64x256`) tile's `split(n, nb, n, 8)` moves
  under `specialize(cond)` (`split(..., 1)` on the fallback) so dead fallback
  clones in `2x1`/`1x1` branches do not inflate `w`'s bounds check by 7.
- `packed_sub_scales()`: uses `Internal::load_invariant_bits()` on
  `scale_decoders(rs)` when `!sub` and a lane step spans multiple sub-blocks
  (`wsub < vec`). If a scale decoder's load varies across the block (`~m & all`)
  yet is invariant to any low bit within a step (`m & ((vec / wsub) - 1)`),
  consecutive sub-blocks in a lane step share a stored byte/word at different
  bit offsets (`iq2_xs`, `iq2_s`, `iq1_m`). Staging `k.in_callers({codes})` at
  `(acc, ryo)` vectorized across sub-blocks unpacks the block's scales once with
  constant shifts and vector interleaves instead of per-step `vpsrlvd` variable
  shifts.

G/H (GGML / ours), T1 before -> after:

| format | vec_dot K=4096 | vec_dot K=14336 | mul_mat 4096² M=1 (f32 / q8_K) |
|---|---|---|---|
| q6_K | 0.814 -> 0.964 | 0.809 -> 0.965 | 0.863 -> 0.978 / 0.924 -> 0.984 |
| q2_K | 0.757 -> 0.930 | 0.750 -> 0.924 | 0.728 -> 0.901 / 0.768 -> 0.897 |
| q4_K | 0.910 -> 0.955 | 0.918 -> 0.974 | 1.034 / 1.594 (16x4) |
| q5_K | 1.147 -> 1.195 | 1.125 -> 1.203 | 1.215 / 1.832 (16x4) |
| tq2_0 | 0.893 -> 1.069 | 0.900 -> 1.130 | 1.137 / 1.168 |
| iq2_s | 0.886 -> 1.012 | 0.879 -> 1.014 | 0.838 -> 0.994 / 0.898 -> 1.002 |
| iq2_xs | 0.956 -> 1.061 | 0.961 -> 1.064 | 0.936 -> 1.044 / 1.006 -> 1.042 |
| iq1_m | 0.742 -> 0.786 | 0.748 -> 0.792 | 0.713 -> 0.744 / 0.756 -> 0.960 |

Held-out `vec_dot` (54 cases): 38.9% -> 57.4% matched (31/54 >= 0.95), median
0.908 -> 0.966. `--check` vec_dot and mul_mat (all formats): 0 failures. M3
replay (982 Arm/Metal objects): all byte-identical.

### SWAR unpacking for planar bit-packed sub-block scales (q3_K)

`q3_K` `vec_dot` trailed at 0.78-0.80 of GGML (K = 256/4096/14336) and M = 1
`mul_mat` at 0.87 (4096² T1). Its 16 6-bit sub-block scales are stored planar
across 12 bytes (`scales_lo[t & 7]` shifted by `2 * (t >> 3)`, `m = 8`;
`scales_hi[t & 3]` shifted by `2 * (t >> 2)`, `m = 12`). Because bit 0 of the
sub-block index `t` varies (`!(m & 1)`), the first `packed_sub_scales()` filter
(`m & low`) missed it and re-unpacked the scale bytes inside each of the 8
32-value steps. Staging it as a 16-lane SIMD vector improved `llvm-mca` from
45.8 to 39.9 cycles/block (~0.89x measured), still behind GGML's 36.8 cycles:
the loop is bound on vector ALU Ports 0, 1 and 5 (~39.7 cycles/block), where
SIMD scale unpacking competes directly with the dot product.

GGML unpacks the 12 scale bytes as three 32-bit GPR words (`0x0f0f0f0f` /
`0x03030303` masks and shifts), which execute on scalar ALU Ports 6 and 11 in
parallel with the vector dot product on Ports 0/1/5. In `packed_sub_scales()`,
any scale decoder with partial sub-block invariance (`(~m & all) && (m & all)`)
has contiguous load runs of `run = 1 << ctz(inv)` sub-blocks (`run = 4` for
`q3_K`, where bit 2 is the lowest invariant bit; `run = block / wsub` when bit
0 is invariant). When `run < block / wsub`, `blocked()` schedules the staged
scale clone with `.split(t, kw, kb, run).unroll(kw).vectorize(kb, run,
VectorizeIn::SWAR)` (supported by extending `LowerSWARVectors::narrow()` in
`src/VectorizeSWAR.cpp` to narrow `PlanarFieldPack`'s widened unsigned `Div` and
power-of-two `Mod`). Removing the SIMD scale dependency also lets LLVM's
`MachineCombiner` break the 8-step serial `vpdpwssd` accumulator chain into
parallel `vpmaddwd` + `vpaddd`, dropping `llvm-mca` to 35.4 cycles/block.

G/H (GGML / ours), T1 before -> after:

| format | vec_dot K=256 | vec_dot K=4096 | vec_dot K=14336 | mul_mat 4096² M=1 (f32 / q8_K) |
|---|---|---|---|---|
| q3_K | 0.781 -> 0.976 | 0.783 -> 1.012 | 0.802 -> 1.018 | 0.870 -> 1.044 / 0.934 -> 1.035 |

`--check` vec_dot and mul_mat (`q3_K`, `iq2_xs`, `iq2_s`, `iq1_m`): 0 failures.
M3 replay (982 Arm/Metal objects): all byte-identical.

### Inlining 1-bit-per-lane Tabulate (q5_0, q5_1) and multi-block weight rfactor in blocked() (q1_0)

Two generic bottlenecks in the 32-value activation-block (`q8_0`/`q8_1`)
kernels on x86:

- `q5_0` (0.81-0.85 of GGML) and `q5_1` (0.74-0.84) decode their high bit plane
  through `Tabulate{h, 8, 256}` (created for Arm NEON, which has no bit-unpack
  instruction). On x86, precomputing that 2 KB table emitted 4 scalar byte
  extractions, 4 `vmovq` table loads, and 3 vector shuffles per block (`q5_0`
  `llvm-mca`: 14.3 cycles per 2 blocks vs GGML's 10.7). In `register_lut()` /
  `luts()`, any 1-bit-per-lane `Tabulate` (`t->entries == (1 << t->per)`) is
  inlined on x86 (`target.arch == Target::X86`); LLVM lowers the 32-bit unpack
  to `vpbroadcastd` + `vpshufb` + `vptestnmb` + masked move (`llvm-mca`: 10.6
  cycles per 2 blocks).
- `q1_0` on `q8_0` activations (0.62-0.78 of GGML, M = 1 `mul_mat` 0.81) has
  `fmt.per = 4` (four 32-value `q8_0` blocks per 128-value `q1_0` block).
  `blocked_mmla()` (Arm `i8mm`) split `ry` by `fmt.per` and hoisted the weight
  block's scale across the 4 activation blocks, but `blocked()` (`vec_dot` and
  x86 `mul_mat`) did not: splitting `ry` by `ts.interleave = 2 < 4` left
  `ryo % 2` / `ryo / 2` integer division and modulo addressing inside the inner
  loop (11 scalar ALU instructions per iteration) and reloaded/multiplied `w.d`
  4 times per weight block. Passing `wper = fmt.per` (`t.arch == Target::X86 &&
  !fmt.sub && integer`) to `blocked()` splits `ry` into `(ryw, ryp, wper)`,
  rfactors `(rl, ryw)` into `wblk`, hoists `w.d` into `win =
  wblk.update().hoist_invariants()[0]`, and folds `wblk` and `blk` with
  `eager_inline()`, giving clean pointer induction (`addq $18, %rdi; addq $136,
  %rcx`) and one `w.d` multiply per 4 `q8_0` blocks.

G/H (GGML / ours), T1 before -> after:

| format | vec_dot K=256 | vec_dot K=4096 | vec_dot K=14336 | mul_mat 4096² M=1 / T10 |
|---|---|---|---|---|
| q5_0 | 0.811 -> 0.915 | 0.854 -> 0.960 | 0.853 -> 0.982 | 0.903 -> 0.995 / 0.909 -> 1.060 |
| q5_1 | 0.741 -> 0.839 | 0.817 -> 0.941 | 0.837 -> 0.958 | 0.862 -> 0.972 / 0.930 -> 0.951 |
| q1_0 | 0.623 -> 0.816 | 0.770 -> 0.923 | 0.778 -> 0.927 | 0.812 -> 1.095 / 0.769 -> 1.097 |

`--check` vec_dot, mul_mat, and codec (`q1_0`, `q5_0`, `q5_1`): 0 failures. M3
replay (982 Arm/Metal objects): all byte-identical.



