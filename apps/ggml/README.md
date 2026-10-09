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
its GGML type name in `schemes/schemes.h`; f16 is the `fp16` scheme, f32 has
none) and one row of `GGML_FORMATS` in `formats.cmake`
(`type ops [acts [target-features]]`). A type may name two lossless stages
composed after the quantizer, which knows neither:
`<ggml type>[.<codes>][.<rows>x<chunk>]`. The codes pick the integer encoding
(`i4`: two's-complement nibbles, `twos` in `schemes/common.h`; default: GGML's
offset binary). The layout (`schemes/layouts.h`; default: one row per record,
AoS) is `Interleave{rows, chunk}`: one record per `rows` rows at the same block,
each field's elements interleaved across the rows in `chunk`-byte pieces, as
GGML's repack (`q4_0.i4.4x8` is `q4_0_4x8`, whose XOR 0x88 is the
two's-complement encoding). Its libraries are named by the type with `.` as `_`;
its weights (`[K / block, N / rows]` records) are checked against GGML's repack:
the harness's transcription of it (`harness/repack.cpp`, every layout), itself
checked against the bytes of GGML's `CPU_REPACK` buffer for the layout GGML
picks here.

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
`format(spec, aos, scaled)`, see `Q4_0Quant`'s `code_shift`), and dequantize
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
gemm tiles (smmla's for any M with i8mm, 4 x 16 when the records cover 16 rows,
the weight's decode then shared by the tile's row pairs, else 4 x 8; when N is a
multiple of their width, else 1-wide tiles as tall) starting on a record, with M
tails guarded: `dotr` is bound to one tile's rows, and its rows past the records
read the last activation row (clamped; nothing reads past the input). The rest
run the gemv tiles; with a laid-out integer weight they run `blocked_rows`
instead: one weight record per tile (rows-tall, as GGML's repacked gemv), its
int32 sums vectorized across the record's rows and each piece's 4-code quads
(sdot), so a piece is one dense load and the activation's piece is broadcast to
the rows. An in-kernel activation encoder runs first, parallel over activation
rows (for M > 1) and vectorized per encoded block (`encoder`: the block's
reductions, e.g. its scale, across the block; per-value Funcs, e.g. the codes,
across their values). Kernel ABI (`harness/halide_providers.cpp`): `w`/`a` are
`[K / block, N / rows or M]` records (struct types from the kernel's metadata),
`out` is f32 `[N, M]`. The kernel declares the whole contract (checked in
`checked`, assumed in `bench`): all mins 0, rows dense
(`dim(1).stride == dim(0).extent`), `a`'s K tied to `w`'s, `w`/`a` rows match
`out`'s N/M, and `vec_dot` pins `out` to 1 x 1. It promises no more than GGML
does: K is a whole number of blocks (GGML asserts `n % QK == 0`), with no even
block count (the 2-block interleave keeps its guard) and no host alignment
(q4_0/q8_0 rows start on 2-byte boundaries). `vec_dot` goes through a GGML-ABI
adapter.

Each library has two variants: `checked` (default target features; asserts and
bounds queries on; for `--check` and tests) and `bench` (adds
`GGML_BENCH_FEATURES`, i.e. `no_asserts no_bounds_query`; what gets timed).
Halide providers get the default `Precision` (f32 accumulation, no
re-quantization) plus the act re-quantization of `<storage>:<compute>` acts.
