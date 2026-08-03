# Clean q5_0 Approximation Implementation

## Current status

- Baseline commit: `ca685e94e91c33d924f10c238ffa4ae6dab183a4`
- Status: complete; all acceptance gates passed
- [x] Phase 0: confirm clean baseline and create this progress document
- [x] Phase 1: collect ten paired baseline runs at n=4096
- [x] Phase 2: add stage-key tracing and reusable core Approximation components
- [x] Phase 3: add focused core correctness coverage
- [x] Phase 4: refactor q5_0 composition and schedule lookup
- [x] Phase 5: correctness, odd-tail, generated-code, and full-suite validation
- [x] Phase 6: paired performance validation and durable documentation

## Goal and constraints

Refactor q5_0 so its generator contains only the base reduction,
`approximate_by`, `compute_offline`, and scheduling. Compose all q5_0
representation logic from reusable core Halide Approximations. Preserve
correctness, keep median paired performance within 5% of the committed baseline,
and remain at least 0.90x GGML. q5_1 is explicitly unchanged transitional debt.

## Core Approximation APIs

- Add an opaque, copyable `ApproximationStageKey` to every Approximation
  instance.
- Extend traced invocation through `Compose`, `Apply`, `TrustedInverse`, and
  `Func::approximate_by` so `ApproximationResult` resolves encoded and decoded
  outputs by `(StageKey, port)` while preserving flat `handles` and `encoded`.
- Add public standard components in a dedicated header included by `Halide.h`:
  `StructLayout`, `StorageCast`, `LittleEndianScalarPack`, `BinaryAlphabetPack`,
  `AdditiveRadixSplit`, `PlanarFieldPack`, `BlockReshape`, and the symmetric
  block quantizer/policies.
- Leave ggml compatibility aliases/wrappers so unrelated formats do not migrate.
- Test nested combinators, repeated types with distinct keys, invalid lookups,
  encode/decode lookup, and component correctness, including one- and
  two-dimensional `StructLayout` records.

## q5_0 target composition and schedule

Faithful packed type: `{d: Float16, qh: UInt8[4], qs: UInt8[16]}`.

Outer to inner:

1. `StructLayout`, logical `{qs, qh, d}` to physical fields.
2. `Apply` `StorageCast<Float32, Float16>` to `d`.
3. `Apply` `LittleEndianScalarPack<UInt32>` to `qh`.
4. `Apply` `BinaryAlphabetPack<int8_t>{32, UInt32, -16, 0}` to high
   contributions.
5. `Apply` `PlanarFieldPack{4, 16}` to low nibbles.
6. `Apply` `AdditiveRadixSplit{16, 16}` to signed codes.
7. Symmetric block quantization, qmax 16, extreme-signed scale selection,
   truncate-half-up rounding.
8. `BlockReshape{32, block_indexed}`.

Capture stage keys for reconstructed signed codes (`AdditiveRadixSplit`) and the
qh word (`LittleEndianScalarPack`), carry them through scheme metadata into
`VecDotSpec`, and resolve scheduling Funcs from `ApproximationResult`. Replace
the duplicate q5_0 tail Approximation with stage-scoped eager inlining into the
tail update. Preserve the measured two-block SDOT schedule and use Func
identity, not names, for `sdot_partial` exclusions.

## Validation gates

- q5_0 quantize is bit-exact with GGML; dequantize and vec_dot pass tolerances.
- `kernel-bench --all` has no failures.
- Odd block counts pass at n=32, 96, 160, 224, and 1056.
- ARM assembly contains SDOT, one qh word load per block, contiguous LUT loads,
  no qh byte-load sequence in the main loop, and no accumulator stack spill or
  one-iteration epilogue loop.
- Median paired q5_0 is no more than 5% slower than baseline and at least 0.90x
  GGML; shared formats show no accidental regression.
- Any compiler optimization is general, separately tested, and
  target-independent where possible. No new scheduling directive is planned.

## Benchmark experiment policy

Any experimental setup that proves useful or repeatable should be promoted into
`kernel-bench` as a named mode rather than left as an ad hoc shell recipe. This
includes scaling/size sweeps such as the q5_0 odd-block checks at n=32, 96, 160,
224, and 1056. One-off scripts are acceptable for initial exploration, but the
durable form should make the experiment discoverable, reproducible, and usable
for future regressions from the benchmark utility itself.

## Baseline results

Ten paired filtered runs, `KERNEL_BENCH_N=4096`, filter
`q4_0,q4_1,q5_0,q5_1,q8_0`:

Median of ten per-run timings and median paired ratio:

| Format | GGML CPU   | Halide     | Paired GGML/Halide |
| ------ | ---------- | ---------- | ------------------ |
| q4_0   | 94.437 ns  | 96.893 ns  | 0.9726x            |
| q4_1   | 107.819 ns | 118.424 ns | 0.9020x            |
| q5_0   | 122.172 ns | 130.259 ns | 0.9379x            |
| q5_1   | 143.575 ns | 153.717 ns | 0.9339x            |
| q8_0   | 73.202 ns  | 74.829 ns  | 0.9764x            |

All candidate correctness flags were true. Raw CSV files are in
`/tmp/q50-baseline.tX9dNF` for this work session.

## Experiment log

| #   | Change                                                                                                                | Correctness                                                                                        | GGML / Halide timings                                             | Generated-code observations                                                                                          | Decision  |
| --- | --------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- | --------- |
| 0   | Clean committed baseline                                                                                              | All filtered vec_dot checks passed                                                                 | q5_0: 122.172 / 130.259 ns, 0.9379x paired median                 | Existing q5_0 path is the generated-code reference                                                                   | Reference |
| 1   | Add opaque stage keys and traces through Compose, Apply, TrustedInverse, and approximate_by                           | Nested/repeated/directional/invalid lookup tests pass                                              | Not performance-sensitive                                         | Flat encoded/handle compatibility retained                                                                           | Keep      |
| 2   | Add public core components; move BlockReshape and symmetric quantization behind ggml compatibility alias/wrapper      | Focused component suite passes, including 1-D and 2-D StructLayout and inline StorageCast rounding | Not measured independently                                        | `strict_float` makes fp16 storage rounding survive eager inlining                                                    | Keep      |
| 3   | Replace q5_0 legacy split-code composition with the eight reusable stages                                             | q5_0 quantize/dequantize/vec_dot pass                                                              | Focused q5_0 run: 117.7 / 126.2 ns in one sample                  | Faithful UInt8[4] qh lowers through concat_bits to a word load                                                       | Keep      |
| 4   | Resolve codes/qh by stage key, stop sdot inlining by Func identity, and share/eager-inline the q5_0 tail decode graph | Odd n=32/96/160/224/1056 all pass                                                                  | Included in final medians                                         | Main loop has two blocks in flight, four SDOTs/pair, persistent vector accumulators; tail is scalar and fully inline | Keep      |
| 5   | Full validation and ten final paired runs after the final StorageCast change                                          | `kernel-bench --all` clean; focused tests and odd sizes pass; all paired flags true                | q5_0: 122.147 / 130.645 ns, 0.9349x; baseline delta +0.30% Halide | One unaligned qh word load/block, contiguous LUT loads, no accumulator spill or one-iteration epilogue               | Final     |

## Final paired results

Median of ten n=4096 paired runs:

| Format | GGML CPU   | Halide     | Paired GGML/Halide | Halide vs baseline |
| ------ | ---------- | ---------- | ------------------ | ------------------ |
| q4_0   | 94.187 ns  | 96.832 ns  | 0.9727x            | -0.06%             |
| q4_1   | 106.760 ns | 118.423 ns | 0.9015x            | 0.00%              |
| q5_0   | 122.147 ns | 130.645 ns | 0.9349x            | +0.30%             |
| q5_1   | 143.728 ns | 153.956 ns | 0.9336x            | +0.16%             |
| q8_0   | 72.774 ns  | 74.687 ns  | 0.9744x            | -0.19%             |

Negative deltas are improvements. Raw final CSV files are in
`/tmp/q50-final-strict.vULMku` for this work session.

## Framework/compiler issues

- No compiler change was needed. Existing `LowerStructTypes` handling of
  `concat_bits()` recovered one unaligned qh word load from the faithful
  `UInt8[4]` field.
- The existing Halide build had `WITH_TESTS=OFF`; it was reconfigured with tests
  enabled to build the two focused correctness targets.
- Installing only the development component updates headers and GenGen but not
  the changed shared library; a full `cmake --install` was required before the
  standalone ggml generator could link the new stage lookup methods.
- A plain fp32-to-fp16-to-fp32 cast chain can fuse away when fully inlined.
  `StorageCast` uses `strict_float` on both conversions so storage rounding is
  schedule-independent; its correctness test intentionally leaves the stage
  inline. q5_0 also materializes the fp16 value in its packed struct field.

## Final follow-up items

- Add a `kernel-bench` scaling/odd-tail mode that captures the useful n-sweep
  performed during this work; follow the benchmark experiment policy above for
  future reusable setups.
- Remove the legacy q5-specific components and specialized q5_1 reduction after
  q5_1 is migrated to reusable components; this work intentionally leaves it.
