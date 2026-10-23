# GGML quantized kernels

A harness comparing GGML's quantized `vec_dot` and `mul_mat` kernels (CPU, CPU
repack, KleidiAI, Metal, BLAS) against an f64 oracle and against Halide kernels
registered in `formats.cmake`. GGML is reached through its public API only
(`ggml.h`, `ggml-cpu.h`, `ggml-backend.h`, `ggml-alloc.h`).

## Build and run

```sh
cmake --preset macOS && cmake --build build/macOS -j 6 && cmake --install build/macOS --prefix install/macOS
cmake -S apps/ggml -B build/apps/ggml -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH=$PWD/install/macOS \
	-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build/apps/ggml -j 6
build/apps/ggml/ggml-quant-bench --check # all types, smoke shapes (ctest: ggml_check)
build/apps/ggml/ggml-quant-bench --types q4_0 --shapes llama3-8b-gemv >out.csv
build/apps/ggml/ggml-quant-bench --help
```

The vcpkg port (`apps/vcpkg/ports/ggml`) pins GGML v0.26.0 with `GGML_NATIVE`,
CPU repack, KleidiAI (arm64) and Metal (macOS) on, and `GGML_LLAMAFILE` off, so
the plain CPU path is GGML's own `vec_dot` loop.

## Providers and dispatch

| provider                | what                                                                                                  | `path` column                                  |
| ----------------------- | ----------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| `ggml-vec_dot`          | `ggml_get_type_traits_cpu(w)->vec_dot`, activations in its `vec_dot_type`                             | `vec_dot:<act type>`                           |
| `ggml-cpu`              | CPU backend graph, W in the plain CPU buffer                                                          | `vec_dot:<vec_dot_type>`                       |
| `ggml-cpu-<buft>`       | W in a CPU extra buffer type (`kleidiai`, `repack`), from the `ggml_backend_dev_get_extra_bufts` proc | kernel family/layout, `gemv` (M = 1) or `gemm` |
| `ggml-metal`            | Metal backend graph                                                                                   | Metal pipeline names                           |
| `ggml-blas`             | BLAS (Accelerate) backend; M, N, K >= 32 only                                                         | `blas:sgemm(dequant)`                          |
| `halide-<name>`         | `formats.cmake` row, bench build                                                                      | `halide`                                       |
| `halide-<name>:checked` | same, checked build; `--check` only                                                                   | `halide`                                       |

A provider is skipped for a case it does not support: the backend's
`supports_op` (checked before uploading W: extra buffer types repack in
`set_tensor`) or, for Metal, the probe below. Dispatch is reported from public
signals: the buffer type W was allocated in (as llama.cpp chooses it), GGML's
log (captured with `ggml_log_set`: repack logs
`repack tensor ... with <type>_<rows>x<cols>`), and for Metal, a child process
(`--probe-metal`) that runs the case once and prints the `kernel_mul_m*`
pipelines it compiled (pipelines are compiled once per process, so attribution
needs a fresh one; Metal's `supports_op` ignores src1's type, so a crashing
probe also marks the case unsupported). KleidiAI's kernel names are inferred
from the weight type (`qsi8d32_qsi4c32` for q4_0, `qai8dx_qsi8cx` for q8_0).

## Oracle

The reference is an f64 dot product of GGML's own decoding (`to_float`) of the
inputs, on up to 64 x 4 sampled outputs. The tolerance is derived from what each
provider declares it does (`Precision` in `harness/harness.h`), not tuned:
accumulator precision and chain length, operand rounding, and internal
re-quantization of activations or weights (see `harness/oracle.cpp`). It passes
when error / tolerance \<= 1 (`err_ratio`). With pre-quantized activations the
bound is tight; providers that re-quantize f32 activations get a worst-case
requantization term, which is much looser.

Notable GGML behavior: KleidiAI's q8_0 path re-quantizes weights and activations
per row (it does not compute q8_0 x q8_0); the CPU backend rejects f16
activations for quantized weights; Metal's mul_mv has no quantized x f16 kernels
(mul_mm does).

## Benchmark methodology

- `mul_mat` graphs hold 16 independent `mul_mat` nodes (Metal concurrency
  disabled, so they run back to back like dependent layers) to amortize
  per-graph overhead; times are per node. `--cold` cycles weight copies
  totalling >= 512 MiB so weights stream from DRAM as in inference.
- CPU threads: default = performance cores; the process runs at
  `QOS_CLASS_USER_INTERACTIVE`. Both sides keep idle threads spinning within a
  sample and let them sleep between samples:
  - GGML: a persistent threadpool (default `poll = 50`), resumed before and
    paused after each sample. During a graph, its workers spin at barriers and
    poll for the next graph before sleeping.
  - Halide: the app's copy of Halide's thread pool (`runtime/README.md`), with
    its keep-awake count held for each sample. Idle workers (up to threads - 1)
    and waiting owners poll instead of sleeping, until 4096 polls pass without
    work. Parallel loops on an idle pool take the lock-free fast path, after
    which the workers that took part poll without yielding for some tens of
    microseconds, like GGML's workers at a barrier. `--no-keep-awake` leaves the
    count unheld (ablation), but the fast path still applies while workers spin
    briefly after a loop.
- Per case, each provider's reps are calibrated so a sample takes >= 20 ms, then
  warmed up; then 15 rounds, each timing one sample of every provider in a fresh
  random order. Reported: median, a distribution-free 95% CI of the median
  (order statistics), `ci_pct` = CI width / median, GB/s, GOP/s.
- macOS has no clock pinning: `ci_pct` is the noise report. Serialize timing
  runs machine-wide (`~/dev/Halide/.bench.lock`).

## Formats and codecs

A format is a scheme plus its values per encoded record (`schemes/`, mapped from
its GGML type name in `schemes/schemes.h`; f16 and bf16 are the `fp16` and
`bf16` schemes, f32 has none) and one row of `GGML_FORMATS` in `formats.cmake`
(`type ops [acts [target-features]]`). A type may name two lossless stages
composed after the quantizer, which knows neither:
`<ggml type>[.<codes>][.<rows>x<chunk>|.soa]`. The codes pick the integer
encoding (`i4`: two's-complement nibbles, `twos` in `schemes/common.h`; default:
GGML's offset binary). The layout (`schemes/layouts.h`; default: one row per
record, AoS) is `Interleave{rows, chunk}`: one record per `rows` rows at the
same block, each field's elements interleaved across the rows in `chunk`-byte
pieces, as GGML's repack (`q4_0.i4.4x8` is `q4_0_4x8`, whose XOR 0x88 is the
two's-complement encoding; `q8_0.4x8` is `q8_0_4x8`, no XOR). Its libraries are
named by the type with `.` as `_`; its weights (`[K / block, N / rows]` records)
are checked against GGML's repack: the harness's transcription of it
(`harness/repack.cpp`, every layout), itself checked against the bytes of GGML's
`CPU_REPACK` buffer for the layout GGML picks here. `soa` (`Identity`: the
planar packer's ports kept apart) stores each field in its own array, e.g.
`q4_0.soa`'s codes `[16, K / block, N]` and fp16 scales `[K / block, N]`; each
array is its own kernel argument (the libraries are called through their `_argv`
entry points), checked against GGML's blocks split alike.

`codec` builds the one codec generator (`kernels/codec.cpp`, via `make_codec`
from `tools/halide_approximation_codec.h`) for the type: the scheme's round trip
on a row of floats, severed at the encoded blocks, as `<type>_quantize` (f32
`[K]` -> blocks `[K / block]`, a struct type with GGML's block layout; per
block, vectorized by mul_mat's activation `encoder` schedule) and
`<type>_dequantize`. The `codec` op (`--op codec`, on by default) checks both
bitwise against GGML's reference quantizer (`ggml_quantize_chunk`) and
`to_float` on N(0, 1) rows whose first blocks are adversarial (signed zeros,
ties, constants, tiny and huge values; inf/NaN are undefined in GGML's
reference), and times them against those and `from_float`. It checks the same
for each type's scaled codes (`<type>_quantize_scaled`, `_dequantize_scaled`:
checked variant only; the scheme a kernel may pick with
`format(spec, aos, scaled)`, see `Type0Quant`'s `code_shift`), and dequantize
also on crafted blocks spanning every code byte with every finite fp16 scale
pattern stepped by 31 (zeros, subnormals, both signs). Codec rows reuse the CSV
columns: `atype` is the direction, `err_ratio` is 0 (bit-exact) or inf, `gops`
is Gvalues/s.

`vec_dot` and `mul_mat` build the one mul_mat generator (`kernels/matmul.cpp`,
`weight=<type> act=<act> op=<op>`) for each act in `acts`, as
`<type>_<act>_<op>`: `out(n, m) = sum_k W(k, n) * X(k, m)` in f32 with each
operand approximated by its scheme and severed at its encoded records. An act
`<storage>:<compute>` (e.g. `f32:q8_0`, library `<type>_f32_to_q8_0_<op>`) takes
`<storage>` activations and approximates X by `<compute>`'s scheme inside the
kernel, not severed (GGML's numerics for f32 activations; the harness declares
the act re-quantization in the provider's `Precision`); plain `f32` is the
decode-then-float-dot variant. A `<compute>` with a layout (`f32:q8_0.4x8`,
GGML's x4 activations) splits M as GGML does: `out` is undefined, its first
update takes the rows in whole records (M rounded down to the layout's rows)
from a sum `dotr` over X in that format, its second the rest from `dot` over X
in its AoS form. `kernels/schedule.h` schedules it: the sum is a Func `dot` that
each output tile computes and `out` copies (so `out` is written once, and edge
tiles can shift inwards); per weight block, a dot of the codes (int32, sdot on
Arm, when the activation is quantized alike; f32 otherwise) times the hoisted
scales, as one generic `blocked` schedule of an nt x mt output tile. `vec_dot`
is the 1 x 1 tile for N = M = 1. `mul_mat` (multi-threaded; the harness sets the
thread count) tiles gemm by 4 x 8 outputs of i8mm `smmla` 2 x 2 x 8 tiles
(`blocked_mmla`; when the target has `arm_i8mm` and the activation is quantized
alike; N >= 4, M >= 8; the intermediates' storage is split by `split_storage` so
each 2 x 2 sub-tile is dense and the tile's accumulators stay in registers),
else by 4 x 4 (N, M >= 4), with 32 x 32 outputs per parallel task; otherwise
(gemv) it is a 2 x 1 tile (N >= 2; two rows share each activation block) or the
1 x 1 tile, with 2 blocks per iteration and 16 rows per parallel task (32 tasks
at N = 512 balance 8-12 threads). Tiles at the edges of N and M shift inwards
(they overlap the previous tile, recomputing a few outputs), so any token count
M >= 8 runs on full 4 x 8 tiles. With a laid-out act, the whole records run the
gemm tiles (smmla's for any M with i8mm: 4 x 12, 4 x 16 or 4 x 20, whichever
computes the fewest rows past the records (about equal cost per row), 16 x 4 for
one record, 4 x 8 for two, with each task's tiles covering up to 128 rows so the
weights stream once; per block, all of the tile's sums, then its scales; when N
is a multiple of their width, else 1-wide tiles as tall) starting on a record,
with M tails guarded: `dotr` is bound to one tile's rows, and its rows past the
records read the last activation row (clamped; nothing reads past the input).
The rest run the gemv tiles; with a laid-out integer weight they run
`blocked_rows` instead: one weight record per tile (rows-tall, as GGML's
repacked gemv), its int32 sums vectorized across the record's rows and each
piece's 4-code quads (sdot), so a piece is one dense load and the activation's
piece is broadcast to the rows; the activation's codes are staged per block in
registers, so with 4-code pieces (`q4_0.i4.4x4`, GGML's `q4_0_4x4`) the
broadcast is a by-element sdot. Its gemm tiles (the same choice of shapes) use
the same by-element sdot, as KleidiAI's dotprod gemm: per block the weight
record is decoded once and the sums are computed per activation record, its
codes staged in memory order (a 16-byte load holds a piece of each of its 4
rows) and its 4 scales one vector. The default layout is `q4_0.i4.4x4`, for gemv
and gemm alike (a weight is repacked once, as KleidiAI's is); `q4_0.i4.4x8`
(smmla gemm, at parity on M3) and AoS are built with mul_mat as comparisons.
q8_0 has the same layouts (`q8_0.4x4`, `q8_0.4x8`, `q8_0.soa`) on the same
recipes, its codes read as stored (gemv: 8-byte pieces are a broadcast
half-register, plain sdot). `q4_K.8x8` is GGML's `q4_K_8x8` (8 rows'
super-blocks, each sub-block's 6-bit scales and mins regrouped across the rows
by the lossless `Transpose`, `schemes/common.h`), with q8_K acts (gemv:
`blocked_rows_sub`, one record per tile) and `f32:q8_K.4x8`, GGML's path for f32
acts: `q8_K.4x8` is GGML's `block_q8_Kx4` (q8_K's `Interleave{4, 8}`: 4 rows'
`d`, their `qs` in 8-byte pieces and their `bsums` in pieces of 4), whose codec
is also checked bitwise against the harness's transcription of
`quantize_mat_q8_K_4x8` and GGML's own `ggml_quantize_mat_t`. With i8mm its
whole records run `blocked_mmla` over records (one weight record by one act
record per tile, 8 x 4 outputs as GGML's, 128 act rows per task): each smmla act
operand is one 16-byte load (a row pair's 8-byte pieces), each sub-block's
scales and mins are decoded once per tile as a vector across the record's rows
(its scale factors one 16-lane shuffle of them), and the mins' products read the
act's `bsums` (`derived_sums`: per weight sub-block, pairwise adds of whole
`bsums` vectors, dense across the act's rows); the rest of M (up to 3 rows) runs
the gemv. `q5_K.8x8` is GGML's `q5_K_8x8` on the same recipes (`kx8_layout`,
`schemes/kquants.h`: its `qh` interleaved as `qs`); its gemv unrolls all of a
block's sub-blocks, not pairs, since its high bits' positions vary per sub-block
(`bit_position_varies`: constant shifts). `q6_K.8x8` is GGML's `q6_K_8x8` (its
16 int8 scales per row regrouped across the rows, a `Transpose` per 64-byte
block); its gemv groups 8 sub-blocks (128 values: those sharing `ql`/`qh`
bytes). With act records the gemm loops over such groups of sub-blocks (q4_K's
pairs, q6_K's 128 values; fully unrolled, the codes' loads are scheduled early
and spill), each unrolled with those reading the same bytes adjacent
(`sharing_order`: q6_K's 0, 4, 2, 6, ...). An in-kernel activation encoder runs
first, parallel over activation rows when M > 1, and vectorized per encoded
block (`encoder`: the block's reductions, e.g. its scale, across the block;
per-value Funcs, e.g. the codes, across their values). Kernel ABI
(`harness/halide_providers.cpp`): `w`/`a` are `[K / block, N / rows or M]`
records (struct types from the kernel's metadata), `out` is f32 `[N, M]`. The
kernel declares the whole contract (checked in `checked`, assumed in `bench`):
all mins 0, rows dense (`dim(1).stride == dim(0).extent`), `a`'s K tied to
`w`'s, `w`/`a` rows match `out`'s N/M, and `vec_dot` pins `out` to 1 x 1. It
promises no more than GGML does: K is a whole number of blocks (GGML asserts
`n % QK == 0`), with no even block count (the 2-block interleave keeps its
guard) and no host alignment (q4_0/q8_0 rows start on 2-byte boundaries).
`vec_dot` goes through a GGML-ABI adapter.

Affine formats (`q4_1`: codes * d + min) use the same algorithm and recipes. In
each recipe, `block_dot` (`schedule.h`) inlines all but the codes' decode into
the per-block dot and splits it on the weight's codes
(`Stage::hoist_invariants({codes})`, its decode Funcs tagged `codes`, see
`schemes/tags.h`): the codes' dot (int32 sdot times the hoisted d_w, as for
q4_0) and the rest, the min times the block's act sum (int32, sdot against ones,
when the act is quantized; else of its values), computed in the same block loop,
once per act column (broadcast across the tile's rows). A factor common to both
(d_a) hoists before the split, so each block adds d_a * (d_w * codes + min \*
sum) to the accumulator: one FMA, not two. Formats without such terms (q4_0,
q8_0) are not split. q8_1 is activation-only (a codec row; q4_1's acts). Its
scheme (`schemes/legacy.h`) declares `s`, the block's sum, as a derived port
(`Derive`): `s = fp16(fp16(d) * sum(qs))`, the sum of the decoded block rounded
once to fp16. GGML's `s = fp16(d * sum(qs))` uses the unrounded d and is an fp16
ulp off ours in about a quarter of the blocks (the codec check compares `d` and
`qs` bitwise with GGML and `s` with our formula, and reports how many blocks
differ); decode ignores `s`. Where the act has a derived port, `vec_dot` takes
the weight's code-free terms out of the dot before tiling (`derived_sums`): per
block, the min times the act's block sum, which
`Func::approximate_by(DerivedPort, ...)` replaces with a read of `s` after
matching it against the port's definition (real-valued; the error is the
declared fp16 rounding of `s`). This is generic: any scheme's `Derive` whose
definition matches the outlined sum is read. `mul_mat` keeps the computed act
sum (once per act column, amortized over the tile's rows), and q4_1 with f16/f32
acts has no derived port. `ggml_scheme_properties` (ctest) checks q8_1's
properties without its layout: requantizing is idempotent, `s` holds, every
stage is closed; and with `s` derived GGML's way, requantizing changes `s` in
about a quarter of the blocks.

q5_0 and q5_1 are q4_0's and q4_1's quantizers at 5 bits (`Type0Quant`,
`Type1Quant`: GGML's "type-0" and "type-1"), each unsigned code split
(`BitSplit`, `schemes/common.h`) into its low nibble, packed as q4's (`qs`), and
its fifth bit, packed densely (`BitPack`: `qh`, bit j of its little-endian word
is code j's). Their kernels are the same recipes, with no new schedule code.

mxfp4 (`schemes/mx.h`) is OCP MX's FP4 as GGML stores it: per block of 32, an
e8m0 scale byte `e` and the e2m1 codes, packed as q4_0's. Its quantizer
(`MXFP4Quant`) is GGML's: `e = (uint8_t)(floorf(log2f(amax)) - 2 + 127)`
(wrapping for tiny blocks; log2f as correctly rounded: double, rounded to
float), `d = 2^(e - 128)`, and each code the first of `kvalues_mxfp4` (the
doubled e2m1 values) nearest `x / d` (`best_index_mxfp4`'s strict scan of
`|kv * d - x|`, fused as GGML's arm64 build computes it). Its lossless stages:
the codes `e2m1_codes()`, `kvalues_mxfp4` as a `codebook` (`schemes/common.h`:
int8 codes and their indices in a constant table; the table itself is computed
from the e2m1 MiniFloat by `minifloat_integers`), decoded by a `mux` of the
table's constants, which Halide lowers to a register table lookup (NEON `tbl`,
as GGML's `vqtbl1q_s8`), so the kernels' sdot takes them as int8, as q4_0's; the
scale `e8m0_half`, an e8m0 MiniFloat of `2d` decoded GGML's way
(`GGML_E8M0_TO_FP32_HALF`; byte 0xFF is 2^127, not NaN). The codec is bit-exact
against GGML's reference quantizer. Its kernels are the same recipes, with no
new schedule code. `ggml_mxfp4_properties` (ctest) checks the error bound, the
declared ranges, idempotent requantizing and closure on normal, tiny, huge,
one-signed, outlier, constant and zero blocks.

iq4_nl and iq4_xs (`schemes/iq.h`) are `IQ4Quant`, GGML's search
(`quantize_row_iq4_nl_impl` as `ggml_quantize_chunk` runs it: no importance
matrix, `ntry = 7`) step for step, with codes drawn from `kvalues_iq4nl` (a
`codebook`, decoded by `tbl` as mxfp4's): iq4_nl a block of 32 under an fp16
scale; iq4_xs a super-block of 256 whose sub-blocks of 32 have 6-bit scales
(`SumSplit` into a low nibble and 2 high bits) under an fp16 one. nvfp4
(`schemes/mx.h`, `NVFP4Quant`) is a block of 64, each sub-block of 16 with a
ue4m3 scale (a `MiniFloat` with GGML's quirks, `ue4m3_half`) and e2m1 codes as
mxfp4's. tq2_0 and tq1_0 (`schemes/ternary.h`, `TernaryQuant`) are codes in {-1,
0, 1} under an fp16 scale per 256 values (GGML's MAX scan, NaN and all, and
`lroundf` as arm64's saturating `fcvtas`), stored as 2-bit fields (tq2_0) or
five base-3 digits per byte (tq1_0: `Segments` splits the block into its `qs`
and `qh` runs, `TritPack` packs each). q1_0 (`schemes/binary.h`, `SignQuant`) is
the signs of 128 values (`BitPack{1}`) under their mean magnitude (GGML's
sequential float sum, kept serial by the encoder schedule). All six codecs are
bit-exact against GGML's quantizers and dequantizers; their kernels are the
existing one-level and two-level recipes, the q8_0 acts of iq4_nl, nvfp4 and
q1_0 a divisor of the weight's block (nvfp4: 2 per block, q1_0: 4) and float
sub-block scales (nvfp4, iq4_xs) multiplied in per sub-block. nvfp4's ue4m3
scales decode through a table of each byte's (as clang compiles GGML's NEON
vec_dot), and its vec_dot and gemv take GGML's lanes: one per (sub-block, act
block) of a pair of act blocks, each a sub-block's sdot, then all four scaled by
one vector multiply-add (`blocked_sub_lanes`). `ggml_iq_properties`,
`ggml_mxfp4_properties` (nvfp4) and `ggml_ternary_properties` (tq1_0, tq2_0,
q1_0) check their bounds, ranges and closure.

The grid formats iq2_xxs, iq2_xs, iq2_s, iq3_xxs, iq3_s, iq1_s and iq1_m
(`schemes/grid.h`) decode natively and encode by GGML's quantizer. Their decode
(`dequantize_row_iq*`) is a `ScaledCodes` stage (int8 codes times an odd
sub-block scale times the fp16 one) over lossless units: each group of 8 codes
(iq3: 4) is a point of a lattice grid (`Lattice`: GGML's `kgrid_*` lists, their
digits mapped to values by a `codebook`), decoded through a constant table of
the points' codes (`Tabulate`: a row per point, an embedded Buffer), times signs
(iq2_xxs, iq2_xs, iq3_xxs: 7 bits of even parity per 8 values, `EvenSigns`,
GGML's `ksigns_iq2xs`; iq2_s, iq3_s: 8 raw bits) or, for iq1, plus a ±1/8 delta
per run (`RunShift`, codes at 8 times their value). The indices' high bits,
signs and scales are bit fields of GGML's structs (`BitFields`, `BitSplit`,
`BitPack`); iq1_m's fp16 scale is spread over the top nibbles of its 4 scale
words (`fp16_bits`, `Inverse`). The encode is `TrustedInverse` (core): an extern
stage (`ExternQuantize`, `define_extern` of `ggml_trusted_quantize` in
`harness/trusted.cpp`) calling `ggml_quantize_chunk`, paired with the native
decode. GGML's grid quantizers are searches, not compositions: per group they
pick signs by parity (flipping the least-weighted value), try 13–31 trial
scales, round each group to the grid's integer lattice and, off the grid, search
the neighbour list of the point (`iq2xs_init_impl` / `iq3xs_init_impl` build a
map of the 43692 (iq2, iq1) or 4096 (iq3) lattice points and their nearest grid
points at init); iq1 sorts each group and searches all two-cut partitions into
{-1, 0, 1} (O(n²) per 32 or 16 values) with both delta signs; all weighted by an
importance matrix. Only the codec's quantize pipelines call the extern; the
kernels read weights GGML quantized. GGML requires an importance matrix for
iq2_xxs, iq2_xs and iq1_s (`ggml_quantize_requires_imatrix`): the harness's is
synthetic and deterministic (`importance`: column k's weight 1 + h(k) / 64, h(k)
the top byte of k · 2654435761, in \[1, 5)), used for the reference quantizer,
the weights and the extern alike; the others take GGML's no-imatrix path, as
llama-quantize without one. Their kernels are the two-level recipe with the
tables cloned into the sub-block dots (`tables()`), acts q8_K (their
vec_dot_type), f16, f32 and f32:q8_K; `ggml_grid_properties` checks the decode's
declared ranges through GGML's quantizer and that every grid and the even sign
table are bijections on their indices.

KleidiAI's q8_0 scheme (`schemes/kleidiai.h`; GGML's KleidiAI backend runs it
for q8_0 weights) is two rowwise formats, a whole row of K values one
quantization group: `qsi8cx` (weights: symmetric int8 codes, an f32 scale per
row, and the row's code sum as a derived port, as KleidiAI's packer stores it)
and `qai8dx` (acts: asymmetric int8 codes, a zero point and an f32 scale per
row, `kai_lhs_quant_pack_qai8dxp_f32`'s arithmetic). K is known only at run
time: `format(spec, ..., blocks)` takes the row's block count as an `Expr` (the
weight's extent), and the rows stay in QK-value blocks, so the reduction is the
other formats' `(r.x, r.y)` and only the per-row fields drop the block. Their
layouts are AoS/`soa` (codes `[QK, K / QK, N]`, each field `[N]`) and
`<rows>x<chunk>` for the codes (`qsi8cx.4x8` is the codes of KleidiAI's
`qsi8cxp4x8`). `rowwise_mul_mat` (`schedule.h`) splits the dot (`rowwise_dot`)
into the codes' dot and the act's offset times the weight's code sum, which it
reads from the weight's derived port, so the dot is int32 over all of K and the
scales apply once per output, as KleidiAI's kernels: gemv tiles of 4 rows (sdot;
with `qsi8cx.4x8`, a record piece is one load and the act's piece a broadcast),
gemm tiles of 8 x 8 and 4 x 8 smmla. The in-kernel encoder (`f32:qai8dx`;
`rowwise_encoder`) computes each row's range (per-lane vectors over the blocks),
scale and zero point, then its codes, parallel over rows; with i8mm its codes
are stored in groups of 8 rows (padded, repeating the last row), each group's
8-code pieces adjacent and the group's K contiguous (`split_storage`), so an
smmla operand is one load and a tile reads one stream; its tiles start on a
group (the tail tile guarded). Pre-quantized acts: 8 x 8, 4 x 8 and 4 x 2. The
harness times them under GGML's q8_0
(`halide-qsi8cx[_4x8]_[f32_to_]qai8dx_mul_mat`, next to `ggml-cpu-kleidiai`): it
requantizes the q8_0 weights (decoded) to qsi8cx with its codec at setup,
untimed, as GGML's backend does on load. The codec check
(`--op codec --types qsi8cx,qai8dx`, also under `q8_0`) is bit-exact against
GGML's q8_0 -> qsi8cx loop (a copy in `harness/codec.cpp`), on raw rows and on
q8_0-decoded ones, and against KleidiAI's packers (codes, row sums, scales;
`qai8dx`'s codes, offsets and scales from `kai_lhs_quant_pack_qai8dxp_f32`).
Error bounds: half a step for qsi8cx; for qai8dx half a step plus 2^-10 (the
zero point, rounded and clamped, can push a range end up to half a step past
\[-128, 127\]: argued in `Qai8dxQuant`). `ggml_kleidiai_properties` (ctest)
checks the bounds, the declared ranges, the row sum and closure on normal,
one-signed, skewed, outlier, constant and zero rows.

Float weights `f16` and `bf16` are `MiniFloat` schemes (`schemes/common.h`: a
GGML-free minifloat parameterized by exponent and mantissa bits, bias, sign,
inf/NaN encoding, overflow and rounding; `binary16` lowers to `Float(16)`,
`bfloat16` to bit arithmetic stored as `BFloat(16)`) with one value per record.
Their encoders are bit-exact against `ggml_fp32_to_fp16_row` and
`ggml_fp32_to_bf16_row_ref` (RNE, NaNs quieted), exhaustively over all 2^32
floats (`ggml_minifloat_properties`, ctest, which also checks generic shapes
such as e4m3, e5m2 and e8m0). The acts are GGML's: the weight's own type, f32,
and f32 converted in-kernel (`f32:f16`, `f32:bf16`). Without blocks, vec_dot and
mul_mat take `flat` (`schedule.h`): f32 accumulators vectorized along K, the
conversions inlined (`fmlal`/`fmlal2` for f16 x f16; `fcvtl` or `shll` widening
otherwise), mul_mat on 4 x 2, pair and single-row tiles. GGML's `f16` vec_dot
accumulates in f16 on ARM (`--check` models it); ours accumulates in f32.
`f16.acc16` (vec_dot, mul_mat; f16 and `f32:f16` acts) is the same algorithm
with the weight format's `acc`, an `Accumulate` (`schemes/common.h`: a reduction
at a narrower float, `Func::change_type(type, unsafe = true)`, declared lossy),
which `flat_tree` (`schedule.h`) applies with `approximate_by` to GGML's f16
partial sums: K split by 32, an rfactor chain over the step's lanes (4
accumulators of 8; per step one f16 `fmla.8h`, the product fused by
change_type), the accumulators summed (0 + 2) + (1 + 3) in f16, then widened and
summed in f32 as GGML's `vaddvq`. Any format of one 16-bit float per value can
take it. Its rows need `strict_float` (the generator throws without it; LLVM
would otherwise reassociate the tree), and `--check` compares it bit-exact with
GGML's f16 vec_dot where the CPU has fp16 arithmetic and K % 32 == 0 (GGML sums
the rest in f64; ours in f16).

Each library has two variants: `checked` (default target features; asserts and
bounds queries on; for `--check` and tests) and `bench` (adds
`GGML_BENCH_FEATURES`, i.e. `no_asserts no_bounds_query`; what gets timed).
Halide providers get the default `Precision` (f32 accumulation, no
re-quantization) plus the act re-quantization of `<storage>:<compute>` acts.
