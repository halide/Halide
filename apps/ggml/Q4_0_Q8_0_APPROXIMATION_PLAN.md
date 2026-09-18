# Clean q4_0 and q8_0 Approximation Implementation

## Current status

- Source baseline commit: `ca685e94e91c33d924f10c238ffa4ae6dab183a4`
- Working baseline: completed q5_0 core-composition refactor in this worktree
- Status: complete; all acceptance gates passed
- [x] Phase 0: inspect the existing schemes and create this progress document
- [x] Phase 1: collect ten paired baseline runs at n=4096
- [x] Phase 2: compose q4_0 and q8_0 from reusable core components
- [x] Phase 3: add focused correctness coverage
- [x] Phase 4: preserve identity-based scheduling and tuned tails
- [x] Phase 5: correctness, odd-tail, generated-code, and full-suite validation
- [x] Phase 6: paired performance validation and durable documentation

## Goal and constraints

Apply the q5_0 cleanup methodology to q4_0 and q8_0. Their schemes should use
faithful packed struct types and reusable public Halide Approximations; the
generic vec-dot generator should retain only the base reduction,
`approximate_by`, `compute_offline`, and scheduling. Preserve bit-exact
quantization, dequantization/vec-dot correctness, and the tuned four-block SDOT
shape. Keep median paired performance within 5% of this worktree baseline and at
least 0.90x GGML.

Unrelated legacy formats remain compatibility debt. In particular, q1_0 keeps
the legacy symmetric layout until it is migrated deliberately, and q4_1/q5_1
remain on their affine/legacy paths.

## Target compositions

q4_0 faithful type: `{d: Float16, qs: UInt8[16]}`.

1. `StructLayout`, logical `{qs, d}` to physical fields.
2. `Apply` `StorageCast<Float32, Float16>` to `d`.
3. `Apply` `PlanarFieldPack{4, 16}` to stored nibbles.
4. `Apply` a reusable additive-offset component mapping signed codes to on-disk
   `[0, 15]` values.
5. `SymmetricBlockQuantize`, qmax 8, extreme-signed scale selection, and
   truncate-half-up-with-offset rounding.
6. `BlockReshape{32}` with the requested row/block-indexed layout.

q8_0 faithful type: `{d: Float16, qs: Int8[32]}`.

1. `StructLayout`, logical `{qs, d}` to physical fields.
2. `Apply` `StorageCast<Float32, Float16>` to `d`.
3. `SymmetricBlockQuantize`, qmax 127, absolute-max scale selection, and nearest
   rounding.
4. `BlockReshape{32}` with the requested row/block-indexed layout.

The standalone q8_0 codecs and q8_0 weight path use the faithful core scheme.
The shared activation ABI remains byte-addressed: a struct-typed experiment
broadened generated-code changes across q4_0/q5_0 without removing reusable
representation logic from either target's weight/codec pipeline. Mismatched
consumers also require that byte path for the existing `Reblock` component.

## Validation gates

- q4_0 and q8_0 quantize outputs are bit-exact with GGML; dequantize and vec-dot
  pass existing tolerances.
- Focused component tests cover the additive offset and both compositions.
- `kernel-bench --all` has no failures.
- Odd block counts pass at n=32, 96, 160, 224, and 1056.
- ARM main loops retain SDOT, four blocks in flight, wide contiguous code loads,
  persistent accumulators, and fully unrolled fixed-size epilogues without
  accumulator stack spills or one-iteration epilogue loops.
- Median paired performance is no more than 5% slower than baseline and remains
  at least 0.90x GGML; q5_0 and affine shared-format checks show no accidental
  regression.

## Benchmark experiment policy

Any useful or repeatable experimental setup must be promoted into `kernel-bench`
as a named mode rather than left as an ad hoc shell recipe. Size and scaling
sweeps used here should feed the same scaling/odd-tail mode already identified
by the q5_0 work.

## Baseline results

Ten paired filtered runs at `KERNEL_BENCH_N=4096`:

| Format | GGML CPU   | Halide     | Paired GGML/Halide |
| ------ | ---------- | ---------- | ------------------ |
| q4_0   | 92.585 ns  | 95.784 ns  | 0.9666x            |
| q4_1   | 104.726 ns | 118.332 ns | 0.8850x            |
| q5_0   | 119.370 ns | 128.931 ns | 0.9258x            |
| q5_1   | 135.748 ns | 153.946 ns | 0.8818x            |
| q8_0   | 70.618 ns  | 74.870 ns  | 0.9432x            |

All candidate correctness flags were true. Raw CSV files are in
`/tmp/q48-baseline.4xCRUB` for this work session.

## Experiment log

| #   | Change                                                                                                                              | Correctness                                               | GGML / Halide timings                                                                                 | Generated-code observations                                                                              | Decision                                         |
| --- | ----------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| 0   | Completed q5_0 worktree baseline                                                                                                    | All filtered vec-dot checks passed                        | q4_0: 92.585 / 95.784 ns, 0.9666x; q8_0: 70.618 / 74.870 ns, 0.9432x                                  | Existing four-block SDOT paths are the generated-code reference                                          | Reference                                        |
| 1   | Add core `AdditiveOffset` and compose faithful q4_0/q8_0 structs from public components                                             | Component composition and standalone q4_0/q8_0 tests pass | Ten-run probe: q4_0 94.177 / 96.975 ns, 0.9712x; q8_0 72.942 / 74.824 ns, 0.9748x                     | Core stages simplify to the existing signed-code SDOT inputs                                             | Keep                                             |
| 2   | Use faithful struct q8_0 for the shared activation operand                                                                          | Correct, including q5_0                                   | Single sample moved absolute timings with core placement; paired ratios did not indicate a regression | Broadened input/load-shape changes across q4_0 and q5_0                                                  | Revert; keep the established byte activation ABI |
| 3   | Share the traced weight and activation decode graphs between q4_0/q8_0 main and tail updates, eagerly inlining only the tail update | Standalone and odd-size tests pass                        | Ten-run probe: q4_0 96.039 / 99.147 ns, 0.9687x; q8_0 75.801 / 77.443 ns, 0.9788x                     | Removes duplicate tail Approximation graphs; main remains four-block SDOT and the remainder stays scalar | Keep                                             |
| 4   | Full validation and ten final paired runs                                                                                           | `kernel-bench --all` clean; all paired flags true         | q4_0: 95.088 / 97.000 ns, 0.9803x; q8_0: 72.973 / 74.951 ns, 0.9736x                                  | Eight SDOTs/four blocks, paired 128-bit code loads, persistent accumulators, no accumulator spill        | Final                                            |

## Final paired results

Median of ten n=4096 paired runs:

| Format | GGML CPU   | Halide     | Paired GGML/Halide | Halide vs baseline |
| ------ | ---------- | ---------- | ------------------ | ------------------ |
| q4_0   | 95.088 ns  | 97.000 ns  | 0.9803x            | +1.27%             |
| q4_1   | 106.761 ns | 117.951 ns | 0.9051x            | -0.32%             |
| q5_0   | 122.167 ns | 130.589 ns | 0.9355x            | +1.29%             |
| q5_1   | 143.560 ns | 153.727 ns | 0.9339x            | -0.14%             |
| q8_0   | 72.973 ns  | 74.951 ns  | 0.9736x            | +0.11%             |

Negative deltas are improvements. Raw final CSV files are in
`/tmp/q48-final.5IktYc` for this work session.

## Framework/compiler issues

- No compiler change was needed. The simplifier folds q4_0's core
  `AdditiveOffset` and `PlanarFieldPack` into the same mask/shift/vector-add
  operations consumed by SDOT, and q8_0's signed struct array lowers to direct
  128-bit loads.
- A struct-typed q8_0 activation was correct but unnecessarily broadened load
  shape changes across q4_0/q5_0. The stable shared activation ABI remains the
  compatibility byte path; q8_0's codecs and weight path are fully
  core-composed.
- Shared q4_0/q8_0 tails must be eagerly inlined into the tail update before the
  main SDOT schedule is applied. The remainder is deliberately scalar and its
  cost scales with one to three blocks; this reinforces the need for a named
  size-sweep benchmark mode.

## Final follow-up items

- Add the reusable size/scaling experiments from this work as named
  `kernel-bench` modes.
- Migrate q1_0 from `StructBlockLayout` and make `Reblock` struct-aware before
  removing the symmetric compatibility layout and byte activation path.
