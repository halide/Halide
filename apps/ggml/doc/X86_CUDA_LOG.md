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

### Per-chunk table staging, 1-block interleave, and chunk splitting in blocked() (iq2/iq3/iq1)

On x86, the 7 grid formats (`iq2_xxs`, `iq2_xs`, `iq2_s`, `iq3_xxs`, `iq3_s`,
`iq1_s`, `iq1_m`) take `blocked()` without sub-block rfactor (`!sub`), stepping
`rxc` across the 8 32-value chunks of a 256-value super-block. Three generic
bottlenecks in `blocked()` on x86 (`chunk_tables`: `!sub && t.arch ==
Target::X86 && block > vec && codes.defined() && table_loads > 0`):

- `tables()` staged every `Tabulate` clone at `LoopLevel(acc, ryo)` (one
  256-byte buffer per table per block). Even with `rxc` unrolled, producing all
  64-96 narrow table loads (`vmovq` / `vmovd` + `vpinsrd`) before consuming any
  of them spilled up to 480 bytes of vector registers to the stack per block
  (`iq2_xxs` 0.82-0.83 of GGML). Staging `tables()` per chunk (`LoopLevel(codes,
  rxc)`) shrinks each table buffer to 32 bytes and lets LLVM interleave table
  loads with the dot product.
- For `table_loads > 8 && sdec.empty()` (`iq3_xxs`: a `per = 4` grid table plus
  a `per = 8` signs table = 12 narrow table loads per chunk, 96 per block),
  fully unrolling all 8 chunks into one basic block still caused LLVM's MI
  scheduler to hoist table loads across chunks and spill `%ymm` registers
  (0.90-0.92x). Splitting `rxc` into `(rxco, rxci, 2)` and unrolling `rxci` (2
  chunks = 64 values per iteration, with `tables()` at `LoopLevel(codes, rxci)`)
  eliminates all vector spills inside the loop (0.98-0.99x).
- Setting `il = wper > 1 || chunk_tables ? 1 : ts.interleave` ensures all
  `chunk_tables` kernels use 1 super-block per outer iteration across `vec_dot`,
  `q8_K_mul_mat`, and `f32_to_q8_K_mul_mat` (where `iq1_m` and `iq3_s` `vec_dot`
  and `iq1_s`/`iq1_m`/`iq3_s` `f32:q8_K` `mul_mat` had previously defaulted to
  `interleave = 2`, doubling live state and spilling).

G/H (GGML / ours), T1 before -> after:

| format | vec_dot K=256 | vec_dot K=4096 | vec_dot K=14336 | mul_mat 4096² M=1 (f32:q8_K) |
|---|---|---|---|---|
| iq2_xxs | 0.775 -> 0.957 | 0.819 -> 0.947 | 0.827 -> 1.000 | 0.840 -> 0.998 |
| iq3_xxs | 0.836 -> 0.979 | 0.882 -> 0.989 | 0.889 -> 0.991 | 0.861 -> 0.983 |
| iq2_xs | 0.952 -> 1.158 | 0.946 -> 1.241 | 0.977 -> 1.213 | 0.936 -> 1.213 |
| iq2_s | 0.886 -> 0.979 | 0.894 -> 0.971 | 0.895 -> 1.011 | 0.838 -> 0.964 |
| iq3_s | 0.897 -> 1.003 | 1.052 -> 1.098 | 1.097 -> 1.098 | 0.822 -> 1.089 |
| iq1_s | 0.806 -> 0.902 | 0.859 -> 0.911 | 0.832 -> 0.916 | 0.705 -> 0.960 |
| iq1_m | 0.676 -> 0.933 | 0.743 -> 0.891 | 0.774 -> 0.883 | 0.697 -> 0.926 |

`--check` vec_dot, mul_mat, and codec (all 7 grid formats): 0 failures. M3
replay (982 Arm/Metal objects): all byte-identical.

### Float sub-block scales at wide = 32 (nvfp4) and vectorized block-sum spart (q4_1)

Two remaining gaps in `blocked()`:

- `nvfp4` on `q8_0` activations (0.76 of GGML at K = 4096/14336; 32-value
  blocks, 16-value sub-blocks with float scales from a 256-entry `ue4m3` LUT)
  fell back to `wide = 16` (`sub = 16 % 32 != 0`) and took `blocked_sub_lanes`
  because `sub * per = 64 > vec = 16`, executing two 16-byte `vpdpbusd` steps
  per block (`%xmm`) with shuffled int32 reductions instead of a single 32-byte
  `%ymm` `vpdpbusd` across both 16-value sub-blocks. Admitting
  `float_sub_scales(rs)` to `wide = 32`, setting `sub = 0` when `block <= vec`
  (a single lane step covers the whole block, so there are no steps across
  sub-blocks to sum before scaling; on Arm NEON `vec = 16 < block = 32` is
  unchanged), and staging `scale_decoders(rs)` (`run = 1`, unrolled in
  `k.in_callers({codes, blk})`) so each sub-block's scalar LUT scale is hoisted
  into a `vbroadcastss` instead of gathered per lane drops `nvfp4_q8_0_vec_dot`
  from 290 ns to 172 ns at K = 4096 (0.764 -> 1.281x GGML).
- `q4_1` on `q8_1` activations (0.83 of GGML at K = 4096, 168 ns vs 140 ns) has
  no `Tabulate` or signed-code fixup uops on Port 5 (`uops_dispatched.port_0 =
  81%`, `port_1 = 85%`, `port_5_11 = 48%`; `llvm-mca` Port 0/1 pressure 10.74
  cycles vs Port 5 4.53 cycles per 2 blocks), because 8 scalar `vcvtsh2ss` and
  2 scalar `vfmadd231ss` instructions dispatch 10 uops exclusively to Ports 0
  and 1. In `schedule_sums()`, when `vec >= 32 && ts.interleave > 1 && vec_sums`
  (`!tabulate` in `rs[0]`, so Port 5 is not occupied by `vpshufb + vptestnmb`
  bit-plane unpacks as in `q5_1`), vectorizing `spart` over `v` (`ryi`) with
  `d.blk` eager-inlined replaces 4 scalar `vcvtsh2ss` + 2 `vfmadd231ss` with
  `<2 x half>` `vcvtph2psx` + `vfmadd231ps`, shifting uops onto Port 5 and
  dropping `llvm-mca` block throughput from 10.0 to 8.5 cycles per 2 blocks.

G/H (GGML / ours), T1 before -> after:

| format | vec_dot K=256 | vec_dot K=4096 | vec_dot K=14336 | mul_mat 4096² M=1 (dot / f32) |
|---|---|---|---|---|
| nvfp4 | 0.869 -> 1.164 | 0.764 -> 1.281 | 0.763 -> 1.306 | 0.811 -> 1.325 / 0.807 -> 1.414 |
| q4_1 | 0.820 -> 0.820 | 0.830 -> 0.947 | 0.981 -> 1.115 | 0.982 -> 1.109 (aos) |

Held-out `vec_dot` and M = 1 `mul_mat` (`analyze.py --l3` / `--by-m`, cases
below 0.97 re-run, minimum kept), baseline `f1000684e` -> now:

| row | T | stratum | before (matched / cases / pct / median) | after (matched / cases / pct / median) |
|---|---|---|---|---|
| `vec_dot` | 1 | format-specific | 21 / 54 / 38.9% / 0.908 | 41 / 54 / 75.9% / 0.990 |
| `mul_mat@M1` | 1 | blocked-dot | 93 / 181 / 51.4% / 0.951 | 163 / 182 / 89.6% / 1.085 |
| `mul_mat@M1` | 1 | format-specific | 18 / 35 / 51.4% / 0.956 | 27 / 34 / 79.4% / 1.002 |
| `mul_mat@M1` | 12 | blocked-dot | 104 / 189 / 55.0% / 0.970 | 151 / 190 / 79.5% / 1.075 |
| `mul_mat@M1` | 12 | format-specific | 13 / 27 / 48.1% / 0.941 | 18 / 26 / 69.2% / 1.173 |

(Each M = 1 row pair totals 216 cases at both revisions; one case moves from
format-specific to blocked-dot. Only vec_dot and M = 1 were re-run;
M = 8/32/512 were not re-measured at this revision.)

`--check` vec_dot, mul_mat, and codec (all 2392 non-CUDA cases): 0 failures. M3
replay (982 Arm/Metal objects): all byte-identical.


### Arm smmla lost behind loaded-bounds annotations (correction)

Merged into `ggml-quant` (`181cb5d04`), this branch made the M3's M = 32
gemm slower: q5_0 2.0x, q5_1 1.7-1.8x, the iq formats 1.4-1.8x (M = 1 and
Metal unaffected). The "M3 replay byte-identical" checks above compared each
change with the M3 libraries generated at `0f37e02fb`, mid-branch, not with
the merge base `d8f92521b`, so they missed the branch's earlier core changes.
Bisecting the 8 regressed Arm objects from `d8f92521b` to `0f37e02fb`:

- `71717f0e2` (iq1_s, iq1_m, iq2_s, iq3_s) and `fecbc0001` (q5_0, q5_1):
  `FindIntrinsics` wraps a staged byte operand of a `widening_mul` (or of a
  Mul by a sign) in `unsafe_promise_clamped` carrying its Func's value
  bounds, for x86's `vpdpbusd`/`psignb` choices. On Arm, the `smmla` matcher's
  `tile_rows()` pushes shuffles down through lane-wise ops (`shuffle_lanes`)
  to prove that one operand's rows repeat; the annotation was opaque to it,
  so the tile wasn't found and each 2x2 tile was computed by `smull` +
  `addp` + `sadalp` (q5_0: 32 `smmla` -> 128 `smull`, 96 `addp`).
- `9046eba86` (iq4_nl, iq4_xs; the `madd()` reference-quantizer rounding):
  one `ldp` split into two `ldr`s and a two-instruction reorder; noise.

Fix (core): `unsafe_promise_clamped` is lane-wise (its bounds are per lane),
so CodeGen_ARM's `is_lane_wise()` lists it, and `shuffle_lanes` sees
through it. Test: `correctness/arm_dot_product_loaded_bounds` (a staged
operand in [0, 15] in a 2x2 tile: fails without, no `smmla`).

The 982 M3 objects, now vs the merge base's generator (958; 24 are layouts
added on this branch): the 8 regressed q5_0/q5_1/iq1/iq2_s/iq3_s objects
(bench and checked, `q8_*` and `f32_to_q8_*` acts) are byte-identical again.
CPU objects that still differ have the same `smmla`/`sdot`/`usdot`/`tbl`
counts and differ in spills and register allocation (K-quants, the 8x8 and
4x4 layouts, mxfp4, iq4), except q2_0 (32 `smull` -> 32 more `sdot`) and
tq1_0 (`saddlv` reductions -> `addp`/`sadalp`, 5% fewer instructions).
The Metal kernels also differ (shader source from this branch's changes);
not investigated, since Metal timings were unchanged. x86 objects are
unaffected (CodeGen_ARM only). Arm timings not measured here (no hardware).

## Phase 3: CUDA on an RTX 3090 (sm_86, 82 SMs; i9-7900X, Fedora toolbox, CUDA 13.4, native Linux)

The Phase 2 numbers above are an RTX A4500's under WSL2; everything below is
this GPU's, on b09e9eaf3. GGML / Halide is GGML's time over ours, f32 acts
unless noted (what GGML's CUDA backend runs), `--rounds 9`, medians of 3 runs.

- Baseline, 4096^2, M = 1, 2, 4, 8, 16, 32, 64, 128, 512: f16 x f16 1.02 0.96
  0.97 0.88 0.89 0.95 0.58 0.56 0.56; f16 on `f32:f16` acts 0.88 0.81 0.81 0.81
  0.73 0.72 0.53 0.55 0.49; q4_0, best library (the exact f32 rows to M = 8,
  `q4_0:f16` on `f32:f16` acts above) 0.63 0.47 0.36 0.43 0.36 0.45 0.41 0.51
  0.40; q4_K 0.61 0.40 0.44 0.48 0.32 0.48 0.49 0.49 0.44. The dp4a rows ran on
  pre-quantized acts only (q4_0 x q8_0 at M = 1, 2, 4, 8: 15.2, 16.9, 23.7, 28.2
  us; GGML's mmvq, its act quantization included, 15.9, 16.2, 17.3, 24.5). f16 x
  f16 at 4096 x 1031 x 32 aborted (`CUDA_ERROR_MISALIGNED_ADDRESS`: `wmma.store`
  on rows of out not 8 bytes aligned); the Phase 2 odd shapes were all below M =
  16 or of N % 8 == 0.

- Core (each with its test, under `HL_JIT_TARGET=host-cuda`):

  - `ExtractWMMAOperations`, `lossless_cast`: a float32 built from a bfloat16's
    bits is a widened bfloat16 operand of a tensor core multiply
    (`wmma_bfloat_bits`).
  - `CodeGen_PTX_Dev`: a dense vector load at an index known to be a multiple of
    its width is 128-bit (or 64-bit) integer loads
    (`gpu_global_vector_load_codegen`).
  - `ExtractWMMAOperations`: int8 products widened from 16 bits
    (`wmma_widened_product`); elementwise functions of tensor-core accumulators
    and of matrices loaded as one (`wmma_elementwise`: the integer gemm's
    scaling of a block's int32 tile into the float32 one).
  - `VectorizeIn::SWAR` in CUDA kernels, widened fields narrowed
    (`gpu_swar_codegen`, `vectorize_swar`).
  - `CodeGen_PTX_Dev`: a dp4a's operands that are sums, differences, shifts and
    masks of vectors of 8- or 16-bit integers filling a register are computed in
    the register, also when done wider and narrowed (`cuda_8_bit_add`). The
    backend computed them an element at a time (prmt, cvt, add.s16, cvt, prmt):
    q4_0's dp4a gemv loop 875 -> 510 lines of PTX, q4_0 x q8_0 at M = 1 15.2 ->
    13.5 us, M = 8 28.2 -> 24.2. Only there: where the elements are wanted (the
    f16 gemm's decode converts each to float) the packed register is unpacked
    again (q4_0:f16 on `f32:f16` acts at M = 64 0.098 -> 0.122 ms with it).

- Coverage: rows for the nine blocked types GGML's CUDA backend runs that had
  none (iq2_xxs, iq2_xs, iq2_s, iq3_xxs, iq3_s, iq1_s, iq1_m, q1_0, nvfp4), on
  f32 acts, on their vec_dot type's (not nvfp4: its sub-block scales leave the
  integer dot nothing to hoist) and as `<type>:f16`; bf16 (the gemm on wmma's
  bf16 fragments) and f32 weights (`f32`, `f32:f16`). The Metal rows' recipes
  ran unchanged.

- The f16 gemm (`gpu_staged`'s `Staging`, `GemmShape::kstep`, `cuda_shapes`):
  f16 and bf16 operands as stored copied by `cp.async`
  (`MemoryType::GPUSharedAsync`; slower on the A4500, 0.18 -> 0.13 ms at M = 64
  here), acts encoded in the pipeline encoded once by a kernel of their own, K
  steps of 256 or 128 values below M = 64 (a block's time is its steps': a
  step's loads take 1-2 us to arrive whatever their size), 2 warps of 16 x 16 to
  4 of 16 x 64 by M, flat weights from M = 2 on guarded tiles. The gemm runs
  where N % 8 == 0, else the rows. Blocked weights: Phase 2's tiles from M = 32
  (2 to 4 warps; each weight decoded once per 64 or 128 act rows) with plain
  loads for the acts' copies (beside a decode, `cp.async` acts were 1.1-1.25x
  slower), one warp's 16 x 16 on 128-value steps below; `cuda_one_warp`
  (q8_0:f16, q5_0:f16): one warp below M = 128. The rows: one warp per block of
  twice the rows (q4_0 gemv 1.2x); the dp4a rows keep two.

- Rows on f32 acts (`<type>` on `f32:q8_1`): the acts quantized to q8_1 by a
  kernel of their own, a thread per record, then the dp4a rows: GGML's mmvq
  (which also quantizes inside its timed call). The scale's maximum is a vector
  reduction (inlined, it was a loop inside every code's: 365 ms on q8_K's
  256-value records). q8_1 for every type, as GGML: its records are 4 bytes
  aligned, and a q8_K record is 256 values quantized by one thread (30 us at K =
  4096: a thread's loads are serial), so the K-quants take 32-value act blocks a
  sub-block at a time. A second launch costs 2.3 us of host time (256 x 64 x 1:
  1.9 -> 4.3 us a call; GGML 4.6) and the kernel ~3.5 us at 4096^2 x 1.

- Integer gemm (`gpu_int_gemm`, `<type>` on `f32:q8_0.soa`; q4_0, q5_0, q8_0,
  iq4_nl, mxfp4): GGML's mmq. Int8 codes on wmma's int8 fragments, a block's
  int32 tile scaled into the f32 accumulator by the lanes holding it; weight
  codes staged a block per lane (`code_words`: 8 codes a GPR word); K split in 4
  below M = 128 (not with a lut). The K split's reduction extent is rounded down
  to whole steps of the split (`round_extent`; out reads it only where K is
  whole steps), so rfactor's split of it is exact (the Phase 2 blocker): no core
  change is needed. q2_0 (64-value blocks) reads `w` two blocks past its end at
  K = 1024 (checked build: "accessed at 17, which is beyond the max (15)"): not
  registered. No K-quant (sub-block scales, min terms) or offset integer gemm; M
  = 9..15 take the rows.

- `halide-by-m-<type>_f32_cuda`: the `f32:q8_1` rows below M = 16, then the
  integer gemm, else `<type>:f16` on `f32:f16` acts.

- Gate: `--check` of every CUDA library (checked and bench) on M in {1, 2, 8, 9,
  15, 16, 17, 31, 32, 33, 63, 64, 65, 128, 200} x N in {31, 33, 36, 40, 127,
  512} x K in {96, 544, 1024, 768, 1280, 33, 1000}: 95400 results, 0 failures, 0
  unchecked. `ctest -L ggml` 67/67 (`ggml_check`: 3116 results, 0 failures, 53
  unchecked: GGML's cuBLAS f16 chains, the KleidiAI and `acc16` rows), `-L
  ggml_cuda` 56/56; the core tests matching
  `gpu_|wmma_|warp_|cuda_|register_shuffle|atomics|lossless_cast|rfactor|vectorize_swar|specialize|deinterleave`
  under `HL_JIT_TARGET=host-cuda`: 111/111. The 82 Metal libraries' `.stmt`
  (`arm-64-osx-metal`) are byte-identical to b09e9eaf3's; a sample of 36 x86 CPU
  libraries' too, but for the numbering of temporaries in 3.

- Results, 4096^2, GGML / Halide on f32 acts, the best Halide library per cell
  (r: the `f32:q8_1` rows; i: the integer gemm; h: `<type>:f16` on `f32:f16`
  acts; x: the exact f32 rows; bold: >= 0.95; three runs' spread under 5% in
  all but one of 1062 cells, median 0.3%):

| type    | M = 1 | 2     | 4     | 8         | 16        | 32        | 64    | 128       | 512       |
| ------- | ----- | ----- | ----- | --------- | --------- | --------- | ----- | --------- | --------- |
| q4_0    | 0.92r | 0.91r | 0.90r | **1.10**i | 0.88r     | 0.86i     | 0.75i | 0.77i     | 0.65i     |
| q4_1    | 0.93r | 0.92r | 0.83r | 0.95r     | 0.87r     | 0.56r     | 0.52h | 0.71h     | 0.52h     |
| q5_0    | 0.91r | 0.89r | 0.86r | **1.05**r | 0.86i     | 0.80i     | 0.78i | 0.69i     | 0.63i     |
| q5_1    | 0.92r | 0.89r | 0.78r | **0.97**r | 0.90r     | 0.53r     | 0.45h | 0.68h     | 0.53h     |
| q8_0    | 0.93x | 0.94r | 0.83i | **0.96**r | 0.87i     | 0.78i     | 0.78i | 0.63h     | 0.62i     |
| mxfp4   | 0.71r | 0.70r | 0.75i | 0.94i     | 0.63r     | 0.59i     | 0.47i | 0.54i     | 0.58i     |
| iq4_nl  | 0.83r | 0.78r | 0.75r | 0.93i     | 0.71r     | 0.68i     | 0.57i | 0.66i     | 0.60i     |
| q2_0    | 0.66r | 0.66r | 0.87r | **0.99**r | 0.65r     | 0.63h     | 0.54h | 0.65h     | 0.46h     |
| q4_K    | 0.77r | 0.72r | 0.83r | **0.96**r | 0.63r     | 0.62h     | 0.60h | 0.70h     | 0.50h     |
| q5_K    | 0.70x | 0.56r | 0.55r | 0.75r     | 0.52h     | 0.54h     | 0.60h | 0.65h     | 0.52h     |
| q6_K    | 0.77h | 0.68x | 0.66r | 0.84r     | 0.56r     | 0.69h     | 0.70h | 0.67h     | 0.53h     |
| q2_K    | 0.85x | 0.65r | 0.87r | **0.97**r | 0.63h     | 0.81h     | 0.75h | **1.16**h | **0.97**h |
| q3_K    | 0.82x | 0.60x | 0.80r | 0.92r     | 0.45r     | 0.59h     | 0.64h | 0.69h     | 0.59h     |
| iq4_xs  | 0.77r | 0.74r | 0.77r | 0.88r     | 0.62r     | 0.57h     | 0.62h | 0.62h     | 0.46h     |
| q1_0    | 0.72r | 0.69r | 0.89r | **0.98**r | 0.60r     | 0.61h     | 0.50h | 0.59h     | 0.51h     |
| iq2_xxs | 0.39h | 0.36r | 0.50r | 0.57r     | 0.38h     | 0.51h     | 0.46h | 0.50h     | 0.47h     |
| iq2_xs  | 0.48r | 0.49r | 0.59r | 0.71r     | 0.53r     | 0.54h     | 0.57h | 0.55h     | 0.61h     |
| iq2_s   | 0.47r | 0.51r | 0.61r | 0.72r     | 0.52r     | 0.42h     | 0.46h | 0.47h     | 0.58h     |
| iq3_xxs | 0.55x | 0.38r | 0.47r | 0.56r     | 0.41r     | 0.55h     | 0.49h | 0.51h     | 0.46h     |
| iq3_s   | 0.52x | 0.41r | 0.47r | 0.59r     | 0.44r     | 0.50h     | 0.49h | 0.35h     | 0.43h     |
| iq1_s   | 0.62r | 0.58r | 0.70r | 0.78r     | 0.54r     | 0.38r     | 0.43h | 0.39h     | 0.51h     |
| iq1_m   | 0.54r | 0.54r | 0.66r | 0.74r     | **1.84**r | **1.11**r | 0.77h | 0.63h     | 0.57h     |
| nvfp4   | 0.56x | 0.42x | 0.47x | 0.51x     | 0.59h     | 0.75h     | 0.59h | 0.87h     | 0.67h     |

`halide-by-m-<type>_f32_cuda` is that column except where x wins, h below M = 17
or r above 16 (nvfp4 has none). 12 of 207 cells reach 0.95, 8 of them at M = 8.
f16 x f16 1.05 1.06 1.07 0.97 1.02 1.04 0.87 0.78 0.60; f16 on `f32:f16` acts
0.89 0.89 0.89 0.89 0.94 1.07 0.89 0.80 0.64; bf16 x bf16 1.02 0.99 0.99 0.98
1.05 0.97 0.90 0.77 0.99; bf16 on `f32:bf16` acts 0.89 0.90 0.90 0.90 0.94 0.99
0.88 0.79 1.01; f32 as `f32:f16` 0.89 0.91 0.92 0.82 0.96 1.05 0.95 0.90 1.36
(GGML: cuBLAS in f32).

- The `full` preset's 12 layers (K x N from 2048 x 512 to 18944 x 3584), M = 32
  / 512, GGML / Halide: f16 x f16 0.44-1.09 / 0.38-0.59, f16 on f32 acts
  0.47-1.10 / 0.47-0.63, q4_0 (by-M) 0.67-1.14 / 0.55-0.89, q8_0 0.58-1.03 /
  0.51-0.82, q4_K 0.35-0.85 / 0.44-0.73, iq2_xs 0.27-0.66 / 0.46-0.84. At M = 32
  the square layers from 2048 to 4096 reach 0.86-1.07 on f16 and q4_0; N = 512
  and 1024 0.44-0.75 on f16 (few tiles, each bound by its steps of K: the
  integer gemm's K split takes q4_0 to 0.90-1.14 there); K or N >= 14336
  0.54-0.92.
- Where the rest is:
  - M \<= 4, q4_0, q4_1, q5_0, q5_1, q8_0 (0.78-0.94): the act kernel. A call is
    two launches; at 4096^2 x 1 q4_0's rows alone take 13.5 us (9.2 MB of
    weights: ~10 us at the card's bandwidth), GGML's two kernels 15.9, ours
    17.4. The act kernel is a thread per 36-byte record (its 32 loads and stores
    in series); GGML's a thread per value. M = 2, 4: the rows' cost per act
    column (q4_0 x q8_0 13.5, 14.2, 21.9 us at M = 1, 2, 4; GGML's mmvq with its
    act kernel 15.9, 16.2, 17.3).
  - M \<= 8, K-quants and grid formats (0.36-0.97): on q8_1 acts each 32-value
    sub-block's int32 dot is scaled in f32 (on q8_K acts, int32 across the
    block: q4_K at M = 1 13.5 us on q8_K acts, 20.9 on `f32:q8_1`, ~3.5 of them
    its act kernel), and their decodes still have per-element PTX (q5_K's high
    bits by variable shifts, the grid lookups: 280 prmt in q5_K's gemv loop). A
    q8_K act kernel needs a thread per value or lane-parallel reductions; a
    struct-typed record Func is a thread's.
  - M = 9..15 (GGML's mmq): the rows (q4_0 at M = 12 0.87). The integer gemm has
    no guarded tile.
  - M >= 16, q4_0, q5_0, q8_0 (0.62-0.88): one staging buffer per step (the
    copies are awaited before the multiplies), weight codes decoded per tile of
    M; iq4_nl, mxfp4 (0.47-0.71): their lut per block, and no K split.
  - M >= 16, the other 18 types (0.35-0.90; q2_K 0.97-1.13 from 128, iq1_m,
    cuBLAS in GGML, 1.11-1.85 at 16..32): the f16 gemm (decode to f16 per tile,
    f32 accumulators) against int8 mma. Needs sub-block scales and the min term
    (rank 1, from the acts' partial sums) in `gpu_int_gemm`.
  - f16 from M = 64 (0.60-0.89): cuBLAS's f16 accumulators (f32 ones bound M =
    512 at 0.84x on this card).

## Phase 4: the integer gemm for the other blocked types (RTX 3090)

Same machine, measure and conventions as Phase 3 (GGML / Halide, f32 acts,
`--rounds 9`, medians of 3 runs).

- Where the time is (GPU time per kernel from CUPTI's activity records, us,
  4096^2; q4_0 on `f32:q8_0.soa` as Phase 3 left it):

  | M   | GGML: quantize, mmq, stream-k fixup | Halide: act codes, act scales, gemm, sum of the K split |
  | --- | ----------------------------------- | ------------------------------------------------------- |
  | 64  | 4.0, 33.0, 9.3 (46.2)               | 3.9, 3.6, 45.4, 5.5 (58.4)                              |
  | 512 | 16.0, 153.6, 19.5 (189.1)           | 17.4, 18.0, 242.4, none (277.7)                         |

  Launch overhead is not it (wall time less GPU time: 1-3 us on both sides). The
  gemm kernel takes 1.4-1.6x mmq's time, and the acts' two kernels twice GGML's
  one. A hand-written wmma kernel of the same design (16 x 16 x 16 int8
  fragments, accumulators scaled in registers, `cp.async` acts, stream-k) does
  no better than the generated one: its best shapes take 0.062 ms at M = 64 and
  0.358 at 512, the library 0.061 and 0.286. Per 32 values of K and 16 x 128
  outputs (M = 512), each lane of the Halide kernel issues 16 `wmma.load.a` and
  16 `wmma.mma` of 16 values of K, and 9 `wmma.load.c` of f32 scale tiles (8
  loads each: a tile of scales is loaded as a matrix, since which elements a
  lane holds is unspecified), then 64 x (cvt, mul, fma). mmq uses
  `mma.sync.m16n8k32` (half the multiplies per value of K), `ldmatrix` and, the
  fragment layout of `mma.sync` being documented, two scale loads per 16 x 8
  tile. That is the remaining gap of the gemm kernel; neither is reachable from
  wmma, by scheduling or otherwise.

- `gpu_int_gemm` on any blocked weight whose decode is codes times factors, plus
  terms free of the codes: the factors of a block's increment that vary along N
  become a Func staged per step (`factor_funcs`: q4_K's d * sc of the sub-block,
  read from the record), those along M the act's scale. Affine weights (q4_1,
  q5_1, q4_K, q5_K) split on their codes (`hoist_invariants`); the code-free
  terms read `q8_1.soa`'s block sums (the derived port, a third planar array),
  an outer product per block. Sub-blocks of 16 values (q6_K, q3_K, iq2_s, iq1_m)
  scale each fragment's int32 tile, the act's scale their sum. A weight block of
  several act blocks (the K-quants' 256 values) is staged once, its steps of 128
  values the acts' (staged per step with the acts, K-quants at M >= 128 need
  more shared memory than a block has). `code_words` takes a K-quant's 64 values
  of 32 bytes per lane (q4_K at M = 64 0.092 -> 0.072 ms).

- Core: `VectorizeIn::SWAR` narrows a right shift of widened lanes by an amount
  that is the same in every lane and provably within the narrow lanes
  (`vectorize_swar`). q5_K's fifth bits, q6_K's and q3_K's high bits sit at a
  bit position that depends on the sub-block, i.e. on the lane's thread:
  `code_words` could not be scheduled for them. (q5_K at M = 64 0.097 -> 0.076
  ms, q6_K 0.102 -> 0.083, q3_K 0.094 -> 0.080.)

- The acts' kernels: their scales and sums are a kernel each as encoded (the
  gemm's factors decode them as they stage them; as first written the `q8_1.soa`
  acts took five kernels, 13.6 us at M = 64), the reductions in them (the
  scale's maximum of 32 values, the sum of 32 codes) of vectors. GPU time before
  the gemm, M = 64 / 512: q4_0 7.5 -> 5.6 / 35.3 -> 29.0 us; q4_1 7.8 / 33.7
  (GGML 4.0 / 16.0).

- q2_0's integer gemm (Phase 3: `w` read two blocks past its end at K = 1024) is
  registered: its 64-value blocks are two act blocks, which the staging now
  takes whole.

- Not done by scheduling: q2_K (a min per 16 values: `q8_1`'s sums are per 32;
  GGML's mmq quantizes its acts with a scale per 64 and sums per 16), nvfp4
  (float scales per sub-block: no integer factor hoists). iq2_xxs, iq2_xs,
  iq3_xxs, iq3_s and iq1_s run and check on the integer gemm but are slower than
  on the f16 gemm at most M (iq2_xxs 0.52, 0.49, 0.44, 0.49, 0.45 at M = 16..512
  against 0.38, 0.51, 0.47, 0.50, 0.48; their grid lookups are decoded a code at
  a time on both): not registered.

- Core review fixes (the six Phase 3 commits, amended): int8 products computed
  in uint16 are no tensor core multiply (they were lowered as one: -1 * 1 =
  65535); a conversion or copy of an accumulator is elementwise; a scalar read
  of a fragment, a uint32 matrix and a float accumulator of an int32 matrix
  below capability 75 are user errors; the int32 / float32 accumulator layouts
  are checked on 32 x 8 and 8 x 32 fragments too; `rfactor()`'s unguarded
  specialization of the intermediate is made when the pipeline is lowered (or
  asked for), so what is scheduled on the intermediate afterwards holds in it;
  the PTX backend's in-register arithmetic uses `VectorizeSWAR`'s and also takes
  shifts left and sums with a constant widened on one side.

- Gate: `--check` of every CUDA library on the Phase 3 grid: 102060 results, 0
  failures, 0 unchecked; of the integer gemm and by-M libraries alone also on K
  in {256, 768, 1280, 2304, 3072} (9450 results, 0 failures). `ctest -L ggml`
  78/78 (`ggml_check`: 3182 results, 0 failures, 53 unchecked, as Phase 3's),
  `-L ggml_cuda` 67/67; the core tests under `HL_JIT_TARGET=host-cuda` 111/111.
  The 82 Metal libraries' `.stmt` are byte-identical to b09e9eaf3's, the 36
  sampled x86 CPU libraries' but for the numbering of temporaries in 2.

- Results, 4096^2, GGML / Halide on f32 acts, the best Halide library per cell
  (r, i, h, x as in Phase 3; i below M = 16: the integer gemm library's rows, on
  `.soa` acts; three runs' spread under 5% in all 1161 cells, median 0.3%):

| type    | M = 1 | 2     | 4     | 8         | 16        | 32        | 64        | 128       | 512       |
| ------- | ----- | ----- | ----- | --------- | --------- | --------- | --------- | --------- | --------- |
| q4_0    | 0.93r | 0.92r | 0.90r | **1.14**i | 0.88i     | 0.88i     | 0.78i     | 0.80i     | 0.67i     |
| q4_1    | 0.94r | 0.93r | 0.82r | **0.97**r | 0.85r     | 0.69i     | 0.72i     | 0.74h     | 0.62i     |
| q5_0    | 0.90i | 0.89r | 0.81r | **1.07**i | 0.88i     | 0.82i     | 0.80i     | 0.73i     | 0.66i     |
| q5_1    | 0.92r | 0.88r | 0.82r | **0.97**r | 0.90r     | 0.67i     | 0.72i     | 0.72i     | 0.62i     |
| q8_0    | 0.93x | 0.94r | 0.86i | **0.97**i | 0.89i     | 0.80i     | 0.74i     | 0.62h     | 0.65i     |
| mxfp4   | 0.72i | 0.71i | 0.77i | 0.94i     | 0.63r     | 0.60i     | 0.51i     | 0.57i     | 0.58i     |
| iq4_nl  | 0.83r | 0.80i | 0.78i | **0.96**i | 0.72r     | 0.65i     | 0.60i     | 0.70i     | 0.62i     |
| q2_0    | 0.66i | 0.66r | 0.88i | **1.06**i | 0.76i     | 0.79i     | 0.80i     | 0.67h     | 0.65i     |
| q4_K    | 0.77i | 0.73r | 0.86i | **0.97**r | 0.92i     | 0.77i     | 0.70i     | 0.73h     | 0.55i     |
| q5_K    | 0.70x | 0.60i | 0.70i | 0.78r     | **0.95**i | 0.75i     | 0.69i     | 0.67h     | 0.57i     |
| q6_K    | 0.78h | 0.69x | 0.74i | 0.93i     | 0.78i     | 0.71h     | 0.73h     | 0.70h     | 0.59i     |
| q2_K    | 0.85x | 0.65r | 0.88r | **0.99**r | 0.57h     | 0.80h     | 0.75h     | **1.13**h | **0.97**h |
| q3_K    | 0.82x | 0.61i | 0.86r | **1.05**i | 0.81i     | 0.68i     | 0.66i     | 0.74i     | 0.63i     |
| iq4_xs  | 0.78r | 0.76i | 0.79i | 0.95i     | 0.62r     | 0.60i     | 0.65h     | 0.64h     | 0.58i     |
| nvfp4   | 0.55x | 0.42x | 0.47x | 0.51x     | 0.59h     | 0.75h     | 0.59h     | 0.85h     | 0.67h     |
| q1_0    | 0.73i | 0.69r | 0.88r | **1.01**i | 0.89i     | 0.87i     | 0.85i     | 0.87i     | 0.69i     |
| iq2_xxs | 0.40h | 0.37r | 0.51r | 0.58r     | 0.38h     | 0.50h     | 0.46h     | 0.49h     | 0.47h     |
| iq2_xs  | 0.46r | 0.47r | 0.57r | 0.70r     | 0.51r     | 0.54h     | 0.56h     | 0.55h     | 0.61h     |
| iq2_s   | 0.48i | 0.51i | 0.61i | 0.73i     | 0.56r     | 0.49i     | 0.50i     | 0.51i     | 0.59h     |
| iq3_xxs | 0.53x | 0.40r | 0.48r | 0.57r     | 0.41r     | 0.54h     | 0.49h     | 0.50h     | 0.46h     |
| iq3_s   | 0.52x | 0.40r | 0.48r | 0.59r     | 0.44r     | 0.52h     | 0.52h     | 0.45h     | 0.47h     |
| iq1_s   | 0.86r | 0.81r | 0.84r | 0.94r     | 0.63r     | 0.45r     | 0.44h     | 0.40h     | 0.52h     |
| iq1_m   | 0.72r | 0.74i | 0.84i | 0.93i     | **2.19**r | **1.34**i | **0.97**i | 0.76i     | 0.59h     |

17 of 207 cells reach 0.95 (Phase 3: 12). From M = 16, against Phase 3's best:
q4_K 0.63, 0.62, 0.60, 0.70, 0.50 -> 0.92, 0.77, 0.70, 0.73, 0.55; q5_K 0.52,
0.54, 0.60, 0.65, 0.52 -> 0.95, 0.75, 0.69, 0.67, 0.57; q3_K 0.45, 0.59, 0.64,
0.69, 0.59 -> 0.81, 0.68, 0.66, 0.74, 0.63; q6_K 0.56 -> 0.78 at 16, 0.53 ->
0.59 at 512; q4_1 0.56, 0.52 -> 0.69, 0.72 at 32, 64, 0.52 -> 0.62 at 512; q5_1
0.53, 0.45, 0.68, 0.53 -> 0.67, 0.72, 0.72, 0.62 from 32; q1_0 0.60, 0.61, 0.50,
0.59, 0.51 -> 0.89, 0.87, 0.85, 0.87, 0.69; q2_0 0.65, 0.63, 0.54 -> 0.76, 0.79,
0.80 to 64, 0.46 -> 0.65 at 512. `halide-by-m-<type>_f32_cuda` (the `f32:q8_1`
rows below 16, then the integer gemm) is within 0.03 of the table except where
the rows (q4_1, iq4_nl, iq4_xs, iq1_m at M = 16) or the f16 gemm (q4_1, q4_K,
q5_K, q6_K around M = 128; iq4_xs at 64: 0.51 against 0.65; iq2_s, iq1_m at 512)
are faster. The float gemms are as in Phase 3 (ms within 2.1%).

- Below M = 16 the rows changed with the review fixes' in-register arithmetic
  (their PTX differs between the two compilers: iq1_s on `f32:q8_1` 12548 ->
  9512 lines, q3_K 15587 -> 14201, iq2_xs 14715 -> 14187, q5_0 9994 -> 9928):
  iq1_s 0.62, 0.58, 0.70, 0.78 -> 0.86, 0.81, 0.84, 0.94 at M = 1..8, iq1_m
  0.54, 0.54, 0.66, 0.74 -> 0.72, 0.74, 0.84, 0.93; q5_0 at M = 4 lost 5.8%
  (0.0229 -> 0.0242 ms), iq2_xs at M = 1..4 3-4%. Of the gemm cells that were
  their type's best, q8_0's integer gemm at M = 64 lost 4.5% (0.0720 -> 0.0753
  ms) and iq4_nl's at M = 32 3.6% (0.0581 -> 0.0602): the compilers emit the
  same PTX for them, so by this phase's schedule (the acts' scales are decoded
  as they are staged; q4_0's and q5_0's cells gained 2-4% in this phase).
  q2_K:f16 at M = 16 reads 0.0492 ms against Phase 3's 0.0443: same schedule,
  same PTX from both compilers, and Phase 3's three runs were 0.0443, 0.0491,
  0.0443 (now 0.0492-0.0502): a cell with two timings, not a change.

- The `full` preset's 12 layers, M = 32 / 512, GGML / Halide (by-M): q4_0
  0.67-1.21 / 0.60-0.94, q8_0 0.57-1.10 / 0.54-0.87, q4_K 0.46-1.02 / 0.49-0.76
  (Phase 3: 0.35-0.85 / 0.44-0.73), q6_K 0.43-1.05 / 0.53-0.83, iq2_xs 0.25-0.66
  / 0.47-0.83 (its libraries are Phase 3's).

- Where the rest is:

  - M >= 16, the gemm kernel (0.55-0.92): 1.4-1.6x mmq's GPU time, for the
    reasons above (wmma's 16 values of K per multiply and its unspecified
    fragment layout, against `mma.sync.m16n8k32`, `ldmatrix` and scales read per
    element). An integer gemm on `mma.sync` needs a matrix unit of that
    instruction in `ExtractWMMAOperations` and the PTX backend. Affine weights
    load two more factor tiles per tile and block (q4_1 against q4_0 at M = 64:
    55.7 against 44.5 us of GPU time), 16-value sub-blocks scale twice per
    block.
  - The acts' kernels: two or three launches (codes, scales, sums: 5.6-7.8 us at
    M = 64, 29-34 at 512) against GGML's one (4.0, 16.0): each of ours reads the
    acts again. One kernel needs a reduction over a warp's lanes stored beside
    another Func's values.
  - M = 128 (0.57-0.87; the f16 gemm still wins on seven types): the integer
    gemm is unsplit there, on 4 warps of 16 x 64.
  - lut formats (iq4_nl, mxfp4, iq4_xs: 0.51-0.72): no K split, a lut load per
    code.
  - M = 9..15: the rows, as Phase 3. No guarded tile was added: the integer gemm
    at M = 16 is no closer to GGML than the rows at 12 were.
  - M \<= 8: as Phase 3 but for the in-register arithmetic: a second launch and
    a thread per record in the acts' kernel, per-element decodes in the grid
    formats' rows.
  - q2_K, nvfp4, iq2_xxs, iq2_xs, iq3_xxs, iq3_s, iq1_s from M = 16: the f16
    gemm, as Phase 3.

## Phase 5: mma.sync, by hand and in the compiler (RTX 3090)

Same machine and conventions as Phase 4. The question: is `mma.sync` (Phase 4's
diagnosis) the lever on the integer gemm, and does the compiler reach it.

- By hand first. A q4_0 x q8_0 kernel after GGML's mmq (nvcc; `mma.sync`
  m16n8k32 on int8, weights decoded to shared memory per 256 values of K, acts
  quantized by a kernel of its own and staged per 128, scales read per element
  from the operands' shared rows, K split over blocks with a kernel summing the
  parts), varied one thing at a time. 4096^2; ms of a call (its three kernels),
  then the GPU time of the gemm and of the sum of the K split (us, CUPTI);
  shapes are weight rows x act rows of a block / parts of K:

  | variant                                      | M = 16                      | M = 64                      | M = 128                       | M = 512                       |
  | -------------------------------------------- | --------------------------- | --------------------------- | ----------------------------- | ----------------------------- |
  | GGML (quantize, mmq, stream-k fixup)         | 0.0298, 22.1 + 4.4          | 0.0472, 33.1 + 9.3          | 0.0747, 47.0 + 16.1           | 0.1921, 150.9 + 19.4          |
  | Halide on wmma (Phase 4)                     | 0.0339, 25.4 + 2.1          | 0.0606, 44.8 + 5.4          | 0.0937, 79.5                  | 0.2865, 238.8                 |
  | by hand, GGML's formulation                  | 64x16/4, 0.0278, 20.6 + 2.4 | 64x64/2, 0.0422, 33.4 + 3.0 | 64x128/2, 0.0662, 53.2 + 4.8  | 64x64/1, 0.2023, 185.9        |
  | (1) two m16n8k16 per 32 values of K          | 64x16/4, 0.0279, 20.8 + 2.4 | 64x64/2, 0.0429, 34.1 + 3.0 | 64x128/2, 0.0672, 54.3 + 4.8  | 64x64/1, 0.2078, 191.6        |
  | (2) the weights' operand by word loads       | 64x16/4, 0.0286, 21.5 + 2.4 | 64x64/2, 0.0446, 35.9 + 3.0 | 64x128/2, 0.0666, 53.7 + 4.9  | 64x128/1, 0.2118, 194.6       |
  | (2) the acts' operand by `ldmatrix`          | 64x16/4, 0.0276, 20.5 + 2.4 | 64x64/2, 0.0439, 35.2 + 3.0 | 64x128/2, 0.0652, 52.2 + 4.8  | 64x128/1, 0.1996, 182.5       |
  | the weights' operand and scales per multiply | 64x16/8, 0.0307, 22.8 + 3.1 | 64x64/2, 0.0638, 55.0 + 3.0 | 128x128/2, 0.1050, 92.7 + 4.3 | 64x64/2, 0.3562, 306.9 + 31.4 |
  | (3) scales a load per element and operand    | 64x16/8, 0.0326, 24.7 + 3.2 | 64x64/2, 0.0636, 54.7 + 3.1 | 128x128/2, 0.1031, 90.8 + 4.3 | 64x64/1, 0.3514, 335.3        |
  | (5) acts staged by `cp.async`                | 64x16/4, 0.0277, 20.6 + 2.4 | 64x64/2, 0.0407, 32.0 + 3.0 | 64x128/2, 0.0635, 50.5 + 4.8  | 64x128/1, 0.1961, 178.8       |

  (4), the tile: blocks of 128 / 64 / 32 weight rows (8 / 4 / 2 warps) take
  0.0268 / 0.0256 / 0.0254 ms at M = 16, 0.0431 / 0.0390 / 0.0427 at 64, 0.0618
  / 0.0618 / 0.0688 at 128, 0.2163 / 0.2004 / 0.2317 at 512; a warp on 32 weight
  rows and every tile of the acts (128 x 64 blocks of 4 warps) 0.0397, 0.0611,
  0.1941 at M = 64, 128, 512. (5), the order: a step's weights loaded after its
  acts' copies are awaited (the order of the generated kernels) against before:
  0.0264 / 0.0252 at M = 16, 0.0413 / 0.0377 at 64, 0.2035 / 0.1891 at 512. So
  the hand-written kernel is at 0.86-0.92x of mmq's GPU time at M = 16..128 and
  1.05-1.09x at 512 (128 tiles on 82 SMs: stream-k balances them; GGML / ours on
  a call 1.07, 1.12, 1.13, 0.95, the best of the variants 1.17, 1.26, 1.26,
  1.02). The multiply instruction is not the lever: m16n8k32 against two of 16
  values of K is nothing (the peak of both, operands in registers, is 298 TOPS
  on this card, as wmma's m16n16k16's), `ldmatrix` against word loads 1-6%. The
  loads per multiply are: scales loaded per element (what wmma's `load.c` of a
  scale tile does, its layout being unspecified) cost 1.2-1.7x, an operand
  reloaded per multiply 1.1-1.8x.

- Core: tiles of 16 x 8 x 32 and 16 x 8 x 16 (rows x columns x values of K) of
  8-bit integers in `MemoryType::Tile` are `mma.sync`'s
  (`ExtractWMMAOperations`, `CodeGen_PTX_Dev`; capability 80). Its registers'
  layout is specified, so nothing moves a tile: each lane loads the elements of
  an operand that it holds (words, where K is dense in memory), stores its
  elements of an accumulator, and an elementwise function of accumulators reads
  a matrix in memory at the elements the lane holds, a load per distinct one
  (two per tile of a per-row vector). The shape of a multiply's tiles selects
  the instruction; wmma's shapes lower as before (`mma_sync_matmul`: both
  shapes, int8 and uint8, operands of both signs, staged in fragments or not,
  K-dense or transposed, the scaled gemm and one offset by a matrix, on the
  device against data that differs in every row, column and value of K).

- The integer gemm on it (`gpu_int_gemm` with 16 x 8 fragments of 32 values of
  K, the weights the 16-row operand; 16 values where sub-blocks are 16: q6_K
  checks) is not faster than on wmma, and is not moved. q4_0, ms at M = 16, 32,
  64, 128, 512 on Phase 4's blocks: 0.0333, 0.0411, 0.0618, 0.1022, 0.299 (wmma:
  0.0339, 0.0413, 0.0606, 0.0937, 0.2865); the best of the blocks tried: 0.0567
  at 64 (128 x 64, K in 4), 0.0908 at 128 (64 x 128), 0.294 at 512; q4_K 0.0320,
  0.0539, 0.0687, 0.1253, 0.364 (0.0334, 0.0471, 0.0728, 0.1235, 0.376), q4_1
  0.0404, 0.0508, 0.0715, 0.1165, 0.348 (0.0406, 0.0506, 0.0696, 0.1138, 0.343),
  q5_K and q6_K within the same +-13% (one run each). Its multiply phase is the
  hand-written kernel's (per tile and 32 values of K: two word loads, one
  `mma.sync`, a load of two scales, 4 x (cvt, mul, fma); the weights' operand
  and scales once per block of K), but the multiplies are not where its time is.
  With instructions of the M >= 256 kernel's PTX replaced by moves (4096^2 x
  512, 256 blocks of 64 x 128; and x 128, 64 blocks, one per SM): all of it 258
  us (81), without the multiplies 179 (64), without multiplies and scaling, i.e.
  staging alone, 154 (63.5); the hand-written kernel on the same blocks and
  staging order 212 (84), its staging alone 134 (48). Two blocks sharing an SM
  take 1.6x one's time each (by hand: 1.2x), the time does not depend on the
  steps of K (64-value steps 0.304 ms, 128 0.297) and larger blocks are slower
  (128 x 128 of 8 warps on 64-value steps 0.339 ms). So from M = 16 the
  generated gemm is bound by moving its operands into shared memory (60% of the
  kernel at M = 512), then by the multiplies themselves (58 us of them at the
  card's peak), and the scales' loads Phase 4 blamed come third.

- `halide-by-m-<type>_f32_cuda` takes, per type, the library measured fastest at
  the nearest of M = 16, 32, 64, 128, 512 where it is more than 3% ahead of the
  default (the integer gemm, else the f16 gemm above 16; `by_m` in
  `harness/halide_providers.cpp`). GGML / Halide at 4096^2, Phase 4's provider
  -> this one: the rows at M = 16 on q4_1 0.78 -> 0.87, q5_1 0.87 -> 0.89,
  iq4_nl 0.63 -> 0.72, iq4_xs 0.57 -> 0.62, iq1_m 1.61 -> 2.18, at 32 on iq1_s
  0.35 -> 0.46; the f16 gemm at 128 on q4_1 0.69 -> 0.73, q4_K 0.63 -> 0.72,
  q5_K 0.61 -> 0.66, q6_K 0.65 -> 0.68, at 64 on iq4_xs 0.51 -> 0.64, at 512 on
  iq2_s 0.52 -> 0.59 and iq1_m 0.51 -> 0.59, at 16 on q2_K 0.52 -> 0.60. Not
  q6_K's f16 gemm at M = 32 and 64, 8% and 4% faster at 4096^2: on the `full`
  preset's 4096 x 1024 x 32 and 2048 x 512 x 32 its integer gemm (K split) is
  1.5-1.7x faster. A choice by M alone is 4096^2's; the other cells were not
  measured on other shapes. Elsewhere the provider is within 3% of the best
  library in every cell from M = 16 but iq2_s at 16 (3.3%) and iq4_xs at 128
  (3.6%).

- Gate: `--check` of every CUDA library on the Phase 3 grid: 102060 results, 0
  failures, 0 unchecked; of the integer gemm and by-M libraries also on K in
  {256, 768, 1280, 2304, 3072} (9450 results, 0 failures). `ctest -L ggml`
  78/78, `-L ggml_cuda` 67/67; the core tests matching
  `gpu_|wmma_|mma_|warp_|cuda_|register_shuffle|atomics|lossless_cast|rfactor|vectorize_swar|specialize|deinterleave`
  under `HL_JIT_TARGET=host-cuda`: 112/112. The 82 Metal libraries' `.stmt` are
  byte-identical to b09e9eaf3's, the 36 sampled x86 CPU libraries' but for the
  numbering of temporaries in 2.

- Results: the libraries are Phase 4's, re-measured (4096^2, M = 1..512, 23
  types, medians of 3 runs): no cell of its best-per-cell table, nor a by-M
  provider's, is more than 3% slower (the worst 2.6%: iq1_s's rows at M = 32,
  0.0701 -> 0.0719 ms); 19 of 207 cells reach 0.95 (17: q6_K and iq1_s at M = 8
  moved from 0.93-0.94 to 0.96-0.97). The float gemms likewise (63 cells, the
  worst 2.3%). The `full` preset's 12 layers, M = 32 / 512, by-M (one run: the
  machine was in use during the other two): q4_0 0.67-1.21 / 0.59-0.94, q8_0
  0.57-1.10 / 0.54-0.88, q4_K 0.46-1.02 / 0.49-0.76, q6_K 0.43-1.04 / 0.53-0.82,
  iq2_xs 0.27-0.66 / 0.47-0.84: Phase 4's.

- Where the rest is, from M = 16: the generated gemm's staging, as above. By
  hand, GGML's formulation with its staging reaches GGML (the table), so the
  schedule's steps are what to change next: what a block stages per step and in
  what order (a step's weights are loaded after its acts' copies are awaited, 7%
  by hand), its blocks sharing an SM, and stream-k's balance at M = 512; then
  the scales' loads, which `mma.sync`'s tiles already remove. Below M = 16 as
  Phase 4.
