# The format table. One row per format (schemes/schemes.h):
#
#   type  ops  [acts  [target-features]]
#
# `target-features` (comma-separated, e.g. metal) are added to the libraries'
# target; the vec_dot and mul_mat libraries' names end in them
# (q4_0_f32_mul_mat_metal), so a format can have both a CPU and a GPU row.
#
# `type`: <ggml type>[.<codes>][.<rows>x<chunk>]. The optional codes pick the
# integer encoding (i4: two's-complement nibbles; default: GGML's offset
# binary), the layout interleaves `rows` rows' records in `chunk`-byte pieces
# (as GGML's repack q4_0_4x8, q8_0_4x4, q4_K_8x8, q5_K_8x8, q6_K_8x8, iq4_nl_4x4 etc.;
# q1_0.4x1 is GGML's q1_0_4x8, its interleave in bits; default: one row per record,
# AoS); its weights are checked against GGML's repack (harness/repack.cpp).
# q8_K.4x8 (GGML's block_q8_Kx4, an activation layout) is checked also
# against GGML's quantize_mat_q8_K_4x8 (its transcription, and GGML's own
# ggml_quantize_mat_t).
# `soa`: one planar array per field (a kernel input each), checked against
# GGML's bytes laid out alike. `acc16` (f16.acc16): the dot's partial sums
# approximated in f16 (an Accumulate), in the order and roundings of GGML's
# NEON f16 vec_dot (where the CPU has fp16 arithmetic and K % 32 == 0,
# checked bit-exact against it); else in f32. Its rows need strict_float (the
# order is its numerics: LLVM may otherwise reassociate the partial sums).
#
# qsi8cx (weights) and qai8dx (acts) are KleidiAI's per-row int8 formats
# (schemes/kleidiai.h; no GGML type): their mul_mat rows are timed under
# GGML's q8_0, whose weights the harness requantizes to qsi8cx at setup, as
# GGML's KleidiAI backend does on load. Their codec is checked against GGML's
# requantization and KleidiAI's packers.
#
# `ops`: codec (quantize + dequantize of a row, checked bit-exact against
# GGML, also with the scaled codes kernels may pick: checked variant only;
# activation-only types such as q8_1 have just this),
# vec_dot and mul_mat (kernels/matmul.cpp with weight=type op=<op>, one
# library per act in `acts`). An act is <storage>[:<compute>]: f32:q8_0 takes
# f32 activations and quantizes them to q8_0 inside the kernel (as GGML's CPU
# paths do); its library is <type>_f32_to_q8_0_<op>. A compute type with a
# layout (f32:q8_0.4x8, f32:q8_K.4x8) quantizes the whole groups of rows to
# its records, as GGML's repacked gemm does, and the rest to the AoS type (its
# gemv). A weight type may likewise be <storage>:<compute> (GPU mul_mat
# only): q4_0:f16 (q8_0:f16, and likewise every blocked Metal format's)
# decodes q4_0 (q8_0) records and rounds each decoded value to f16,
# as GGML's Metal gemm (kernel_mul_mm) does; with f16 acts (f16, f32:f16) its
# library runs that gemm on the GPU's matrix unit for M >= 8 and N >= 32 (and
# its mul_mv's rows otherwise: one kernel); its library is <storage>_to_<compute>_<act>_<op>.
# f16 weights with f16 acts take the same gemm (K % 32 == 0).
# Each generated library
# has a checked variant (default target: asserts and bounds queries on; used
# by --check and tests) and a bench variant
# (GGML_BENCH_FEATURES; what gets timed), both registered with the harness
# (harness/halide_providers.cpp, harness/codec.cpp).
set(GGML_FORMATS
    "q4_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "q8_0 codec,vec_dot q8_0,f16,f32"
    "q8_0 mul_mat q8_0,f32:q8_0"
    "q4_1 codec,vec_dot,mul_mat q8_1,f16,f32,f32:q8_1"
    "q8_1 codec"
    "q8_K codec"
    "q4_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q6_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q5_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q2_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q3_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q5_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "q5_1 codec,vec_dot,mul_mat q8_1,f16,f32,f32:q8_1"
    "mxfp4 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "iq4_nl codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "iq4_xs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "nvfp4 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "tq2_0 codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "tq1_0 codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q1_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "q2_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "iq2_xxs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq2_xs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq2_s codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq3_xxs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq3_s codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq1_s codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq1_m codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q4_0.i4.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "q4_0.i4.4x8 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q4_0.i4.8x8 codec"
    "q4_0.soa codec,mul_mat q8_0,f32:q8_0"
    "q4_0.16x4 codec,mul_mat q8_0,f32:q8_0"
    "q4_1.16x4 codec,mul_mat q8_1,f32:q8_1"
    "q8_0.16x4 codec,mul_mat q8_0,f32:q8_0"
    "q4_1.4x8 codec,mul_mat q8_1,f32:q8_1"
    "q8_0.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "iq4_nl.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "mxfp4.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "q1_0.4x1 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q8_0.4x8 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q8_0.soa codec,mul_mat q8_0,f32:q8_0"
    "q8_K.4x8 codec"
    "q4_K.8x8 codec,mul_mat q8_K,f32:q8_K,f32:q8_K.4x8"
    "q5_K.8x8 codec,mul_mat q8_K,f32:q8_K,f32:q8_K.4x8"
    "q6_K.8x8 codec,mul_mat q8_K,f32:q8_K,f32:q8_K.4x8"
    "q4_K.16x4 codec,mul_mat q8_K,f32:q8_K"
    "q5_K.16x4 codec,mul_mat q8_K,f32:q8_K"
    "iq4_xs.16x4 codec,mul_mat q8_K,f32:q8_K"
    "qsi8cx codec,mul_mat qai8dx,f32:qai8dx"
    "qsi8cx.4x8 codec,mul_mat qai8dx,f32:qai8dx"
    "qai8dx codec"
    "f16 codec,vec_dot,mul_mat f16,f32,f32:f16"
    "bf16 codec,vec_dot,mul_mat bf16,f32,f32:bf16"
    "f16.acc16 vec_dot,mul_mat f16,f32:f16 strict_float"
    "q4_0 mul_mat f32,f16,q8_0 metal"
    "q4_0.soa mul_mat f32,f16,q8_0 metal"
    "q8_0 mul_mat f32,f16,q8_0 metal"
    "q8_0.soa mul_mat f32,f16,q8_0 metal"
    "q4_1 mul_mat f32,f16,q8_1 metal"
    "q5_0 mul_mat f32,f16,q8_0 metal"
    "q5_1 mul_mat f32,f16,q8_1 metal"
    "mxfp4 mul_mat f32,f16,q8_0 metal"
    "iq4_nl mul_mat f32,f16,q8_0 metal"
    "q2_0 mul_mat f32,f16,q8_0 metal"
    "q4_K mul_mat f32,f16,q8_K metal"
    "q6_K mul_mat f32,f16,q8_K metal"
    "q5_K mul_mat f32,f16,q8_K metal"
    "iq4_xs mul_mat f32,f16,q8_K metal"
    "q2_K mul_mat f32,f16,q8_K metal"
    "q3_K mul_mat f32,f16,q8_K metal"
    "f16 mul_mat f16,f32,f32:f16 metal"
    "bf16 mul_mat bf16,f32,f32:bf16 metal"
    "q4_0:f16 mul_mat f32:f16,f16 metal"
    "q8_0:f16 mul_mat f32:f16,f16 metal"
    "q4_K:f16 mul_mat f32:f16,f16 metal"
    "q5_K:f16 mul_mat f32:f16,f16 metal"
    "q6_K:f16 mul_mat f32:f16,f16 metal"
    "q2_K:f16 mul_mat f32:f16,f16 metal"
    "q3_K:f16 mul_mat f32:f16,f16 metal"
    "iq4_xs:f16 mul_mat f32:f16,f16 metal"
    "q4_1:f16 mul_mat f32:f16,f16 metal"
    "q5_0:f16 mul_mat f32:f16,f16 metal"
    "q5_1:f16 mul_mat f32:f16,f16 metal"
    "mxfp4:f16 mul_mat f32:f16,f16 metal"
    "iq4_nl:f16 mul_mat f32:f16,f16 metal"
    "q2_0:f16 mul_mat f32:f16,f16 metal"
)
set(GGML_BENCH_FEATURES no_asserts no_bounds_query)

# Schedule parameters (kernels/matmul.cpp's GeneratorParams) of some of the
# rows' libraries: `type op act params...`. interleave=1 (one block per
# iteration of vec_dot and of mul_mat's gemv tiles, default 2) where measured
# faster on Apple M3 (1.02-2.7x): a q8_K act's block is 292 bytes, so a second
# block's loads fall outside ldur's immediate range and each needs its own
# address. Not a rule on q8_K acts (retimed): with 1, iq3_s's and iq1_m's
# vec_dot 1.14-1.18x slower and gemv 1.07-1.13x, q2_K's, q4_K's, q5_K's,
# iq2_s's and tq1_0's gemv 1.01-1.06x, and with an f32 act (staged by the
# pipeline) every vec_dot 1.03-1.05x; with 2, q2_K's, q4_K's and q6_K's
# vec_dot and iq1_s's gemv as fast or faster (q3_K's vec_dot 1.02x slower).
# sublanes=true (vec_dot and mul_mat's gemv tiles: the sub-blocks across the
# vector lanes, their scales one vector multiply-add per 4 sub-blocks) where
# measured faster (iq2_xs's gemv +14-19%, with interleave=1: with two blocks
# per iteration it spills; iq3_xxs's gemv +1-1.5% on one thread, though its
# vec_dot is now 2.5-3% faster without): it also cuts
# q4_K-q6_K's loops by ~15% in instructions, not in time (latency bound), and
# slows iq4_xs, iq2_s and iq1_m. slp=false (not a GeneratorParam: the
# generator runs with HL_LLVM_ARGS=-vectorize-slp=false) where LLVM's SLP
# vectorizer packs the scalar grid-index arithmetic (shifts and masks of qh)
# into vectors, each index then extracted lane by lane for its table load:
# iq1_s's and iq1_m's loops twice the SIMD instructions, ~15-30% slower (also
# with f32 acts, quantized in the pipeline: gemv iq1_s +18-28%, iq1_m
# +6-10%).
# gpr_scales=true (the 6-bit sub-block scales and mins decoded in GPRs,
# GGML's SWAR): q4_K vec_dot +7-12%, q5_K vec_dot +4-7% and gemv +4-9% (one
# thread); in the gemm tiles, decoded once per block per row (not per
# sub-block per row pair), q4_K gemm +9-18%, q5_K +6-15%. fold_offsets (all by default: a weight's constant code offset,
# tq2_0's -1 or q6_K's -32, times the act's sub-block sums, as GGML's vec_dot
# does, not subtracted per value; tq2_0's and tq1_0's gemm 1.2-1.5x, gemv
# 1.1-1.3x): =gemv on vec_dot and the gemv tiles only (q6_K: its gemm tiles
# 10-20% slower, its sub-block scales times the sums), =none where GGML too
# subtracts per value (q3_K: the fold doubles its loops' instructions). Where
# hoist_invariants() can't prove the split exact (iq1_s's and iq1_m's delta,
# +-1 added to 8 times a grid value, a table's, of unknown bounds), no fold.
# The x86 VNNI cols gemm (blocked_cols) always folds, whatever the knob: it
# stages the unsigned codes (q6_K's 0..63) for plain vpdpbusd, as GGML's tiled
# gemm, rather than signed codes through vpsignb.
# row_pairs=false (mul_mat's gemv on single rows, not row pairs): q6_K's
# gemv 1.02-1.03x on one thread at K = 4096 (row pairs spill), even on ten
# (retimed); q5_K's with f32 acts 1.17-1.21x at any K on one thread,
# 1.11-1.16x on ten (row pairs spill: their loop ~910 loads and ~780 stack
# accesses, vs 2 x ~200 and ~230 in single rows; with f16 acts or q8_K's
# they gain 8-21%); q3_K's
# gemv, with interleave=1 (as GGML's vec_dot, a row at a time: a row's block
# 232 instructions, 149 SIMD, the bound on M3; ~260 in row pairs), 0.94-0.98x
# of GGML's to 1.01-1.03x on one thread, 1.04-1.13x on ten. An act of `*`:
# every act. gemm_index_words=true (the gemm tiles' grid index bytes, nested
# in a computed index, staged a word at a time in GPRs; staged_indices()):
# iq3_s gemm 1.11-1.2x (integer acts), its index bytes' loads the bound; not
# with f16 acts (0.96x), nor its gemv or vec_dot (0.88-0.95x: the shift and
# mask per index cost more than the load, and the words spill), nor iq2_s's,
# iq1_s's or iq1_m's gemm (0.94-1.01x).
set(GGML_SCHEDULE_PARAMS
    "q3_K vec_dot * fold_offsets=none"
    "q3_K mul_mat * fold_offsets=none"
    "q6_K mul_mat * fold_offsets=gemv"
    "q6_K mul_mat q8_K row_pairs=false"
    "q6_K mul_mat f32:q8_K row_pairs=false"
    "q5_K mul_mat f32 row_pairs=false"
    "q3_K mul_mat q8_K interleave=1 row_pairs=false"
    "q3_K mul_mat f32:q8_K interleave=1 row_pairs=false"
    "q6_K.8x8 mul_mat * fold_offsets=gemv"
    "q4_K vec_dot * gpr_scales=true"
    "q4_K mul_mat * gpr_scales=true"
    "q5_K vec_dot * gpr_scales=true"
    "q5_K mul_mat * gpr_scales=true"
    "q4_0 mul_mat f32:q8_0.soa code_words=true"
    "q5_0 mul_mat f32:q8_0.soa code_words=true"
    "q8_0 mul_mat f32:q8_0.soa code_words=true"
    "q4_1 mul_mat f32:q8_1.soa code_words=true"
    "q5_1 mul_mat f32:q8_1.soa code_words=true"
    "q4_K mul_mat f32:q8_1.soa code_words=true"
    "q5_K mul_mat f32:q8_1.soa code_words=true"
    "q6_K mul_mat f32:q8_0.soa code_words=true"
    "q3_K mul_mat f32:q8_0.soa code_words=true"
    # (q2_K at 4096^2 x 16 / 32 / 64 0.037 / 0.051 / 0.068 -> 0.034 / 0.047
    # / 0.062 ms; RTX 3090)
    "q2_K mul_mat f32:q8_0.soa code_words=true"
    # (q2_0 at 4096^2 x 12 / 24 / 40 / 64 / 128 0.0283 / 0.0304 / 0.0411 /
    # 0.0477 / 0.0687 -> 0.0202 / 0.0244 / 0.0341 / 0.0438 / 0.0624 ms; q1_0
    # 0.0194 / 0.0253 / 0.0342 / 0.0430 / 0.0620 -> 0.0230 / 0.0278 / 0.0359
    # / 0.0456 / 0.0651, without; RTX 3090)
    "q2_0 mul_mat f32:q8_0.soa code_words=true"
    # (the lut formats' codes looked up in their words: iq4_nl at 4096^2 x
    # 16 / 64 / 512 0.036 / 0.054 / 0.245 -> 0.025 / 0.046 / 0.218 ms, mxfp4
    # 0.039 / 0.065 / 0.275 -> 0.028 / 0.050 / 0.219, iq4_xs 0.035 / 0.051 /
    # 0.238 -> 0.031 / 0.049 / 0.227; RTX 3090)
    "iq4_nl mul_mat f32:q8_0.soa code_words=true"
    "mxfp4 mul_mat f32:q8_0.soa code_words=true"
    "iq4_xs mul_mat f32:q8_0.soa code_words=true"
    "nvfp4 mul_mat f32:q8_0.soa code_words=true"
    # (the dp4a rows' codes a run a GPR word, rows_words, but on q8_K acts
    # q3_K's and q5_K's high bits and iq3_xxs's signs, a bit of a byte per
    # value, do not decode lane-wise in a word; iq1_s's and iq1_m's as fast
    # or faster without: at 4096^2 x 8 iq1_s 0.0277 -> 0.0265 ms. q2_0's
    # 2-bit codes, 4 a byte, a word of 4 (rows_lanes=4; 8 do not lower, a
    # word's two bytes each in 4 lanes): at 4096^2 x 1 / 2 / 8 0.0122 /
    # 0.0127 / 0.0211 -> 0.0110 / 0.0118 / 0.0201 ms; RTX 3090)
    "q2_0 mul_mat * rows_lanes=4"
    # (the dp4a rows' two rows a warp to M = 7 or 6, not 5: at 4096^2 x 6 /
    # 7 q4_1 0.0212 / 0.0231 -> 0.0207 / 0.0221 ms; to 6, iq4_xs at 6 0.0233
    # -> 0.0223, iq1_m 0.0251 -> 0.0243, at 7 0.0238 -> 0.0248, 0.0253 ->
    # 0.0263; q4_0 0.0190 / 0.0200 -> 0.0205 / 0.0227; RTX 3090)
    "q4_1 mul_mat f32:q8_1 rows_narrow=7"
    "iq4_xs mul_mat f32:q8_1 rows_narrow=6"
    "iq1_m mul_mat f32:q8_1 rows_narrow=6"
    "iq1_s mul_mat * rows_words=false"
    "iq1_m mul_mat * rows_words=false"
    "q3_K mul_mat q8_K rows_words=false"
    "q5_K mul_mat q8_K rows_words=false"
    "iq3_xxs mul_mat q8_K rows_words=false"
    "iq3_s mul_mat q8_K gemm_index_words=true"
    "iq3_s mul_mat f32 gemm_index_words=true"
    "iq3_s mul_mat f32:q8_K gemm_index_words=true"
    "q2_K vec_dot q8_K sublanes=true"
    "iq2_xs vec_dot q8_K interleave=1 sublanes=true"
    "q3_K vec_dot q8_K interleave=1"
    "tq1_0 vec_dot q8_K interleave=1"
    "tq2_0 vec_dot q8_K interleave=1"
    "iq2_xxs vec_dot q8_K interleave=1"
    "iq3_xxs vec_dot q8_K interleave=1"
    "iq1_s vec_dot q8_K interleave=1 slp=false"
    "iq1_m vec_dot q8_K slp=false"
    "iq4_xs vec_dot q8_K interleave=1"
    "iq2_xxs mul_mat q8_K interleave=1"
    "iq2_xs mul_mat q8_K interleave=1 sublanes=true"
    "iq2_xs mul_mat f32:q8_K interleave=1 sublanes=true"
    "iq2_xxs mul_mat f32:q8_K interleave=1"
    "iq3_xxs mul_mat q8_K interleave=1 sublanes=true"
    "iq3_xxs mul_mat f32:q8_K interleave=1 sublanes=true"
    "iq1_s mul_mat q8_K slp=false"
    "iq1_m mul_mat q8_K slp=false"
    "iq1_m mul_mat f32:q8_K slp=false"
    "iq1_s mul_mat f32:q8_K slp=false"
    "iq4_xs mul_mat q8_K interleave=1"
    "iq4_xs mul_mat f32:q8_K interleave=1"
    # x86: LLVM's SLP vectorizer packs the u16 grid indices and then
    # extracts each with vpextrw; scalar loads instead (one-thread gemv
    # iq2_xs 0.41 -> 0.69x of GGML, iq2_s 0.36 -> 0.47x, iq3_s 0.48 -> 0.71x)
    "[x86] iq2_xs vec_dot q8_K slp=false"
    "[x86] iq2_xs mul_mat * slp=false"
    "[x86] iq2_s vec_dot q8_K interleave=1 slp=false"
    # (and iq2_s's gemv one row at a time, with its sub-block scales in the
    # lanes: one-thread M = 1 0.71 -> 0.85x)
    "[x86] iq2_s mul_mat * interleave=1 slp=false"
    "[x86] iq3_s vec_dot q8_K slp=false"
    "[x86] iq3_s mul_mat * slp=false"
    # x86: one block per gemv iteration (M = 1, one thread: iq1_s 1.32 ->
    # 1.17 ms, tq2_0 0.33 -> 0.30, iq3_s 2.23 -> 2.11, iq1_m 1.55 -> 1.48)
    "[x86] iq1_s mul_mat q8_K interleave=1"
    "[x86] iq1_m mul_mat q8_K interleave=1"
    "[x86] iq3_s mul_mat q8_K interleave=1"
    "[x86] tq2_0 mul_mat q8_K interleave=1"
    # x86: iq3_xxs's grid index bytes staged in GPR words cost a mov, shift
    # and mask each (and the words spill); a byte load each instead, as
    # GGML's (vec_dot 103 -> 79 cycles a block by llvm-mca, GGML's 69; gemv
    # without sublanes 101 -> 76: sublanes build the grid rows on the stack)
    "[x86] iq3_xxs vec_dot q8_K plain_index_words=false"
    "[x86] iq3_xxs mul_mat * plain_index_words=false"
    "[x86] iq3_xxs mul_mat q8_K sublanes=false"
    "[x86] iq3_xxs mul_mat f32:q8_K sublanes=false"
    # RTX 3090: the CUDA gemm's one-warp tiles below M = 128 (4096^2, f32:f16
    # acts, M = 32 / 64: q8_0:f16 0.084 -> 0.067 / 0.117 -> 0.091 ms; q5_0:f16
    # 0.081 -> 0.079 / 0.117 -> 0.102). No effect on the Metal libraries.
    "q8_0:f16 mul_mat * cuda_one_warp=true" "q5_0:f16 mul_mat * cuda_one_warp=true"
    # RTX 3090: the CUDA integer gemm's tiles to M = 32, below 256 and from
    # 256 (w: wmma's 16 x 16 fragments, m: mma.sync's 16 x 8 tiles, the
    # default; of 4 letters, to 16, to 32, below 256, from 256; of 5, to 16,
    # 32, 64, below 256, from 256: a K split's tiles round up in M, so a
    # range ends at the M its widest tile covers; wmma's fourth from 128),
    # the faster at 4096^2 (ms at M = 16 / 128 / 512, m -> w: q6_K
    # 0.045 -> 0.034 / 0.162 -> 0.094 / 0.309 -> 0.319, q4_K 0.038 -> 0.032 /
    # 0.115 -> 0.090 / 0.279 -> 0.323; q4_0 0.027 -> 0.028 / 0.070 -> 0.083 /
    # 0.234 -> 0.248).
    # q4_K, q5_K: mma.sync's from M = 64 since its K split takes q4_0's tiles
    # (q4_K at M = 128 0.090 -> 0.075 ms), q4_K below 64 too since its weight
    # blocks are staged per step there (M = 32 0.042 -> 0.036), q5_K from 32
    # to 63 (M = 32 0.0420 -> 0.0400, 48 0.068 -> 0.062; at 16 its wmma's);
    # q4_K, iq3_s and iq4_xs on wmma's below 32 (M = 16: q4_K 0.029 ->
    # 0.027, iq3_s 0.038 -> 0.034, iq4_xs 0.031 -> 0.026), q4_1 from 32 to
    # 63 (M = 32 0.0388 -> 0.0374, 48 0.060 -> 0.055).
    # The lut formats (iq4_nl, mxfp4, iq4_xs) on mma.sync's at every M, K
    # split as the others (iq4_nl at M = 16 / 128 / 512, unsplit on wmma's:
    # 0.045 / 0.089 / 0.265 -> 0.033 / 0.074 / 0.247 ms).
    # q6_K, q3_K: mma.sync's from M = 64 (q3_K at M = 64 / 128 / 512 0.062 /
    # 0.090 / 0.291 -> 0.055 / 0.081 / 0.273 ms; q6_K 0.064 / 0.093 / 0.296
    # -> 0.060 / 0.085 / 0.283), and from 32 too (M = 32: q6_K 0.049 ->
    # 0.043, q3_K 0.0404 -> 0.0373; at 16 q6_K 0.030 -> 0.038).
    # The grid formats with 16-value sub-blocks (iq2_xs, iq2_s, iq1_m) on
    # wmma's at every M (iq2_xs at M = 128 / 512 0.177 -> 0.123 / 0.726 ->
    # 0.362) but from M = 64 to 127, where mma.sync's 2 x 2 warps of 32 x 32
    # are (iq2_s at M = 64 0.076 -> 0.061, iq2_xs 0.065 -> 0.055, iq1_m
    # 0.079 -> 0.062; at 128, 0.10 either way), and iq2_xs from 17 to 32
    # (M = 20 / 24 0.0383 / 0.0388 -> 0.0333 / 0.0340; iq2_s 0.0409 /
    # 0.0414 -> 0.0481 / 0.0487); iq1_s below M = 64 (M = 32 0.050 ->
    # 0.044). q2_K: mma.sync's below M = 32 and from 64, wmma's
    # from 32 to 63 (M = 16 / 32: wmma's 0.0339 / 0.0472 ms, mma.sync's
    # 0.0326 / 0.0473, these 0.0326 / 0.0452). q1_0: wmma's to 32 and from
    # 256 (M = 40 / 96 / 128: wmma's 0.044 / 0.066 / 0.070, mma.sync's
    # 0.035 / 0.051 / 0.062; at 12 / 16 / 32 wmma's 3% faster), q2_0
    # mma.sync's (M = 96 / 128 0.078 / 0.109 -> 0.059 / 0.069).
    # int_m24=true (mma.sync's 32 x 24 to M = 24 where 17 to 32 take
    # wmma's): q2_K at 24 0.0428 -> 0.0344 ms, q4_1 0.0357 -> 0.0323, q1_0
    # 0.0249 -> 0.0226, iq1_s 0.0314 -> 0.0297 (mma.sync's to 32: at 32 q2_K
    # 0.0434 -> 0.0456, q4_1 0.0365 -> 0.0389; iq2_s and iq2_xs at 24 0.0416
    # / 0.0384 -> 0.0492 / 0.0405).
    "q4_1 mul_mat f32:q8_1.soa int_tiles=mwmm"
    "q4_K mul_mat f32:q8_1.soa int_tiles=wmmm"
    "iq3_s mul_mat f32:q8_0.soa int_tiles=wmmm"
    "iq4_xs mul_mat f32:q8_0.soa int_tiles=wmmm"
    "q5_1 mul_mat f32:q8_1.soa int_tiles=wmm"
    "q5_K mul_mat f32:q8_1.soa int_tiles=wmmm"
    "q6_K mul_mat f32:q8_0.soa int_tiles=wmmm"
    "q3_K mul_mat f32:q8_0.soa int_tiles=wmmm"
    "q2_K mul_mat f32:q8_0.soa int_tiles=mwmmm"
    "q1_0 mul_mat f32:q8_0.soa int_tiles=wwmmw"
    "iq2_s mul_mat f32:q8_0.soa int_tiles=wwmww"
    "iq1_m mul_mat f32:q8_0.soa int_tiles=wwmww"
    "iq2_xs mul_mat f32:q8_0.soa int_tiles=wmmww"
    "iq1_s mul_mat f32:q8_0.soa int_tiles=wmm"
    "q2_K mul_mat f32:q8_0.soa int_m24=true"
    "q4_1 mul_mat f32:q8_1.soa int_m24=true"
    "q1_0 mul_mat f32:q8_0.soa int_m24=true"
    "iq1_s mul_mat f32:q8_0.soa int_m24=true"
    # int_m40 (from M = 33 to 40 one warp's 32 x 40, K split in 2 or 4; else
    # the K split in 2's 32 x 48 on 2 warps), ms at 4096^2 x 33 / 40: in 4
    # q1_0 0.0329 / 0.0337 -> 0.0310 / 0.0299, iq3_xxs 0.0460 / 0.0469 ->
    # 0.0437 / 0.0445, iq2_xxs 0.0406 / 0.0415 -> 0.0394 / 0.0403; in 2 q2_K
    # 0.0528 / 0.0534 -> 0.0485 / 0.0490, q4_1 0.0408 / 0.0413 -> 0.0388 /
    # 0.0395, q4_0 0.0369 / 0.0355 -> 0.0355 / 0.0342 (the others as fast or
    # slower either way; q4_K, q5_K slower at 33; iq2_s 0.0544 / 0.0520 ->
    # 0.0507 / 0.0479, but at M = 96 0.0725 -> 0.0753;
    # RTX 3090)
    "q1_0 mul_mat f32:q8_0.soa int_m40=4"
    "iq3_xxs mul_mat f32:q8_0.soa int_m40=4"
    "iq2_xxs mul_mat f32:q8_0.soa int_m40=4"
    "q2_K mul_mat f32:q8_0.soa int_m40=2"
    "q4_1 mul_mat f32:q8_1.soa int_m40=2"
    "q4_0 mul_mat f32:q8_0.soa int_m40=2"
)
# A row may lead with an architecture, `[x86] type op act params...` or
# `[arm]`: it applies only when building for that host CPU, and overrides the
# untagged rows' values of the same parameters. The untagged rows were
# measured on Apple M3.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
    set(GGML_SCHEDULE_ARCH x86)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(GGML_SCHEDULE_ARCH arm)
else ()
    set(GGML_SCHEDULE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif ()
# OFF: every library at the GeneratorParams' defaults, the schedule a new
# format gets (to measure what the table adds).
option(GGML_SCHEDULE_KNOBS "Apply GGML_SCHEDULE_PARAMS" ON)
if (NOT GGML_SCHEDULE_KNOBS)
    set(GGML_SCHEDULE_PARAMS "")
endif ()

# CUDA rows (-DGGML_HALIDE_CUDA=ON; the default where GGML has its CUDA
# backend, GGML_CUDA from find_package(ggml)): GGML_FORMATS rows, built for
# GGML_HALIDE_CUDA_FEATURES, registered with the harness and run, checked
# and timed like the Metal rows. The tensor-core gemm is the Metal gemm's
# schedule (schedule.h: gpu_staged, fragments) on wmma's 16 x 16 fragments
# (cuda_shapes), on f16 operands or, for bf16 weights on bf16 acts, wmma's
# bf16 fragments. f32 weights (no scheme) are W itself, or rounded to f16
# for the gemm (f32:f16). A blocked weight's f32:q8_1 library quantizes its
# f32 acts on the GPU, in a kernel of their own, as GGML's CUDA backend does
# (mmvq and mmq run on q8_1 acts), and runs the dp4a rows on them. Most
# also have an integer gemm library, <type> on f32:q8_0.soa acts (schedule.h's
# gpu_int_gemm; as GGML's mmq, which its CUDA backend runs for M > 8): the
# acts quantized on the GPU to q8_0's codes and scales, one planar array each
# (.soa: no records to pack), the weights' int8 codes multiplied by them on
# mma.sync's 16 x 8 int8 tiles or wmma's int8 fragments (int_tiles, by M)
# for M >= 16 and N % 8 == 0 (else the dp4a rows), each
# 32-value block's (or 16-value sub-block's) int32 tile scaled by the weight's
# scale of it (a K-quant's: its sub-block's times its super-block's). Weights
# with an offset or min per block (q4_1, q5_1, q4_K, q5_K) take f32:q8_1.soa
# acts: q8_1's block sums are a third array, which the offsets multiply.
# q2_K's mins are per 16 values (q8_1's sums per 32): on f32:q8_0.soa, the
# act kernel also sums each 16 codes, which its mins multiply. nvfp4's float
# scales per 16-value sub-block scale its sub-blocks' tiles as q6_K's
# integer ones do. code_words=true (GGML_SCHEDULE_PARAMS): its weight
# codes decoded 4 a word in a GPR (the formats decoded by shifts and masks
# of bytes). f16.acc16 (a separate library, declared lossy; GGML's cuBLAS
# f16 gemm accumulates in f16 too): the gemm's accumulators are wmma's f16
# fragments, widened to f32 ones to be stored; its rows f32 as f16's. Each
# f16-, bf16- or .soa-act bench library's test checks that its PTX
# multiplies on the tensor cores (the other acts take dp4a or f32 rows),
# f16.acc16's also that it accumulates in f16.
set(GGML_HALIDE_CUDA_FORMATS
    "q4_0 mul_mat f32,q8_0,f32:q8_1"
    "q8_0 mul_mat f32,q8_0,f32:q8_1"
    "q4_1 mul_mat f32,q8_1,f32:q8_1"
    "q5_0 mul_mat f32,q8_0,f32:q8_1"
    "q5_1 mul_mat f32,q8_1,f32:q8_1"
    "mxfp4 mul_mat f32,q8_0,f32:q8_1"
    "iq4_nl mul_mat f32,q8_0,f32:q8_1"
    "q2_0 mul_mat f32,q8_0,f32:q8_1"
    "q4_K mul_mat f32,q8_K,f32:q8_1"
    "q6_K mul_mat f32,q8_K,f32:q8_1"
    "q5_K mul_mat f32,q8_K,f32:q8_1"
    "iq4_xs mul_mat f32,q8_K,f32:q8_1"
    "q2_K mul_mat f32,q8_K,f32:q8_1"
    "q3_K mul_mat f32,q8_K,f32:q8_1"
    "nvfp4 mul_mat f32,q8_0,f32:q8_1"
    "q1_0 mul_mat f32,q8_0,f32:q8_1"
    "iq2_xxs mul_mat f32,q8_K,f32:q8_1"
    "iq2_xs mul_mat f32,q8_K,f32:q8_1"
    "iq2_s mul_mat f32,q8_K,f32:q8_1"
    "iq3_xxs mul_mat f32,q8_K,f32:q8_1"
    "iq3_s mul_mat f32,q8_K,f32:q8_1"
    "iq1_s mul_mat f32,q8_K,f32:q8_1"
    "iq1_m mul_mat f32,q8_K,f32:q8_1"
    "q4_0 mul_mat f32:q8_0.soa"
    "q8_0 mul_mat f32:q8_0.soa"
    "q5_0 mul_mat f32:q8_0.soa"
    "iq4_nl mul_mat f32:q8_0.soa"
    "mxfp4 mul_mat f32:q8_0.soa"
    "iq4_xs mul_mat f32:q8_0.soa"
    "nvfp4 mul_mat f32:q8_0.soa"
    "q2_K mul_mat f32:q8_0.soa"
    "q6_K mul_mat f32:q8_0.soa"
    "q3_K mul_mat f32:q8_0.soa"
    "q1_0 mul_mat f32:q8_0.soa"
    "q2_0 mul_mat f32:q8_0.soa"
    "iq2_s mul_mat f32:q8_0.soa"
    "iq1_m mul_mat f32:q8_0.soa"
    "iq2_xxs mul_mat f32:q8_0.soa"
    "iq2_xs mul_mat f32:q8_0.soa"
    "iq3_xxs mul_mat f32:q8_0.soa"
    "iq3_s mul_mat f32:q8_0.soa"
    "iq1_s mul_mat f32:q8_0.soa"
    "q4_1 mul_mat f32:q8_1.soa"
    "q5_1 mul_mat f32:q8_1.soa"
    "q4_K mul_mat f32:q8_1.soa"
    "q5_K mul_mat f32:q8_1.soa"
    "f16 mul_mat f16,f32,f32:f16"
    "f16.acc16 mul_mat f16,f32:f16"
    "bf16 mul_mat bf16,f32,f32:bf16"
    "f32 mul_mat f32"
    "f32:f16 mul_mat f32:f16"
    "q4_0:f16 mul_mat f32:f16,f16"
    "q8_0:f16 mul_mat f32:f16,f16"
    "q4_K:f16 mul_mat f32:f16,f16"
    "q5_K:f16 mul_mat f32:f16,f16"
    "q6_K:f16 mul_mat f32:f16,f16"
    "q2_K:f16 mul_mat f32:f16,f16"
    "q3_K:f16 mul_mat f32:f16,f16"
    "iq4_xs:f16 mul_mat f32:f16,f16"
    "q4_1:f16 mul_mat f32:f16,f16"
    "q5_0:f16 mul_mat f32:f16,f16"
    "q5_1:f16 mul_mat f32:f16,f16"
    "mxfp4:f16 mul_mat f32:f16,f16"
    "iq4_nl:f16 mul_mat f32:f16,f16"
    "q2_0:f16 mul_mat f32:f16,f16"
    "nvfp4:f16 mul_mat f32:f16,f16"
    "q1_0:f16 mul_mat f32:f16,f16"
    "iq2_xxs:f16 mul_mat f32:f16,f16"
    "iq2_xs:f16 mul_mat f32:f16,f16"
    "iq2_s:f16 mul_mat f32:f16,f16"
    "iq3_xxs:f16 mul_mat f32:f16,f16"
    "iq3_s:f16 mul_mat f32:f16,f16"
    "iq1_s:f16 mul_mat f32:f16,f16"
    "iq1_m:f16 mul_mat f32:f16,f16"
)
set(GGML_HALIDE_CUDA_FEATURES cuda cuda_capability_80)
# Metal rows need Apple's GPU; CUDA rows GGML's CUDA backend (to compare with)
option(GGML_HALIDE_CUDA "Build, register and run the CUDA libraries" ${GGML_CUDA})
set(_rows "")
foreach (row IN LISTS GGML_FORMATS)
    if (row MATCHES " metal$" AND NOT APPLE)
        continue ()
    endif ()
    list(APPEND _rows "${row}")
endforeach ()
if (GGML_HALIDE_CUDA)
    string(REPLACE ";" "," _cuda "${GGML_HALIDE_CUDA_FEATURES}")
    foreach (row IN LISTS GGML_HALIDE_CUDA_FORMATS)
        list(APPEND _rows "${row} ${_cuda}")
    endforeach ()
endif ()
set(GGML_FORMATS "${_rows}")

set(GGML_HALIDE_LIBRARIES "")
set(registry "")

# Builds the checked and bench variants (`_ggml_variants`, if set: those),
# the generator run with `_ggml_env` (VAR=value...), if set.
function(_ggml_library name gen features)
    if (NOT _ggml_variants)
        set(_ggml_variants checked bench)
    endif ()
    set(features_checked "")
    if (NOT features STREQUAL "-")
        string(REPLACE "," ";" features_checked "${features}")
    endif ()
    set(features_bench ${features_checked} ${GGML_BENCH_FEATURES})
    foreach (variant IN LISTS _ggml_variants)
        add_halide_library(
            ${name}_${variant}
            FROM ggml.generators
            GENERATOR ${gen}
            FEATURES ${features_${variant}}
            PARAMS ${ARGN}
            ENVIRONMENT ${_ggml_env}
            USE_RUNTIME ggml_halide_runtime
        )
        list(APPEND GGML_HALIDE_LIBRARIES ${name}_${variant})
        string(APPEND registry "#include \"${name}_${variant}.h\"\n")
    endforeach ()
    set(GGML_HALIDE_LIBRARIES "${GGML_HALIDE_LIBRARIES}" PARENT_SCOPE)
    set(registry "${registry}" PARENT_SCOPE)
endfunction()

foreach (row IN LISTS GGML_FORMATS)
    separate_arguments(row UNIX_COMMAND "${row}")
    list(POP_FRONT row type ops acts features)
    if (NOT features)
        set(features "-")
    endif ()
    string(REPLACE "," ";" features_list "${features}")
    string(REPLACE "," ";" ops "${ops}")
    string(REPLACE ":" "_to_" id "${type}")
    string(MAKE_C_IDENTIFIER "${id}" id)
    if ("codec" IN_LIST ops)
        _ggml_library(${id}_quantize codec ${features} type=${type} quantize=true)
        _ggml_library(${id}_dequantize codec ${features} type=${type} quantize=false)
        set(_ggml_variants checked)
        _ggml_library(
            ${id}_quantize_scaled codec ${features} type=${type} quantize=true scaled=true
        )
        _ggml_library(
            ${id}_dequantize_scaled codec ${features} type=${type} quantize=false scaled=true
        )
        unset(_ggml_variants)
        string(APPEND registry "GQ_CODEC(${id}, \"${type}\")\n")
    endif ()
    string(REPLACE "," ";" acts "${acts}")
    foreach (op IN ITEMS vec_dot mul_mat)
        if (NOT op IN_LIST ops)
            continue ()
        endif ()
        foreach (act IN LISTS acts)
            string(REPLACE ":" ";" at "${act}")
            list(GET at 0 storage)
            list(GET at -1 compute)
            string(REPLACE ":" "_to_" name "${id}_${act}_${op}")
            if (NOT features STREQUAL "-")
                # (the CUDA capability is not part of the name)
                string(REGEX REPLACE ",?cuda_capability_[0-9]+" "" suffix "${features}")
                string(REPLACE "," "_" suffix "${suffix}")
                string(APPEND name "_${suffix}")
            endif ()
            string(MAKE_C_IDENTIFIER "${name}" name)
            set(params "")
            set(arch_params "")
            foreach (sp IN LISTS GGML_SCHEDULE_PARAMS)
                separate_arguments(sp UNIX_COMMAND "${sp}")
                list(GET sp 0 sp_arch)
                if (sp_arch MATCHES "^\\[(.*)\\]$")
                    list(POP_FRONT sp)
                    if (NOT CMAKE_MATCH_1 STREQUAL GGML_SCHEDULE_ARCH)
                        continue ()
                    endif ()
                    set(sp_list arch_params)
                else ()
                    set(sp_list params)
                endif ()
                list(POP_FRONT sp sp_type sp_op sp_act)
                if (
                    sp_type STREQUAL type
                    AND sp_op STREQUAL op
                    AND (sp_act STREQUAL act
                        OR sp_act STREQUAL "*")
                )
                    list(APPEND ${sp_list} ${sp})
                endif ()
            endforeach ()
            # an architecture's row overrides the untagged rows' same keys
            foreach (ap IN LISTS arch_params)
                string(REGEX REPLACE "=.*" "" key "${ap}")
                list(FILTER params EXCLUDE REGEX "^${key}=")
                list(APPEND params ${ap})
            endforeach ()
            unset(_ggml_env)
            if ("slp=false" IN_LIST params)
                list(REMOVE_ITEM params "slp=false")
                set(_ggml_env "HL_LLVM_ARGS=-vectorize-slp=false")
            endif ()
            if ("metal" IN_LIST features_list)
                list(APPEND _ggml_env ${GGML_HALIDE_METAL_ENV})
            endif ()
            _ggml_library(${name} matmul ${features} weight=${type} act=${act} op=${op} ${params})
            unset(_ggml_env)
            string(APPEND registry
                "GQ_HALIDE_${op}(${name}, \"${type}\", ${storage}, ${compute}, \"${features}\")\n"
            )
            if ("cuda" IN_LIST features_list AND act MATCHES "(f16|\\.soa)$")
                add_test(NAME ${name}_ptx COMMAND grep -q "mma.sync" $<TARGET_FILE:${name}_bench>)
                set_tests_properties(${name}_ptx PROPERTIES LABELS ggml_cuda)
                # (an Accumulate's gemm: wmma's f16 accumulators)
                if (type MATCHES "\\.acc16")
                    add_test(
                        NAME ${name}_ptx_acc16
                        COMMAND grep -q "m16n16k16.f16.f16" $<TARGET_FILE:${name}_bench>
                    )
                    set_tests_properties(${name}_ptx_acc16 PROPERTIES LABELS ggml_cuda)
                endif ()
            endif ()
        endforeach ()
    endforeach ()
endforeach ()
file(CONFIGURE OUTPUT halide_kernels.inc CONTENT "${registry}")
