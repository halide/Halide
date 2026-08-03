# vec_dot performance notes

Working notes for bringing `apps/ggml`'s vec_dot kernels up to `ggml-cpu` speed
on ARM (measured on an M3 Max). Branch `alexreinking/ggml-on-qk`, worktree
`~/dev/Halide/ggml-on-qk`.

## Where things stand

Measured at n=4096, best of many runs (see "Measuring" below):

| type | ggml-cpu | halide   | ratio | before this work |
| ---- | -------- | -------- | ----- | ---------------- |
| q4_0 | 87.8 ns  | 90.7 ns  | 0.97x | 0.97x            |
| q4_1 | 106.7 ns | 110.2 ns | 0.97x | 0.80x            |
| q5_0 | 122.2 ns | 130.2 ns | 0.94x | 0.51x            |
| q5_1 | 143.5 ns | 153.7 ns | 0.93x | 0.50x            |
| q8_0 | 69.1 ns  | 70.4 ns  | 0.98x | 0.95x            |

q5_K (still on the float path) also improved 8615 -> 6730 ns from the same
qh-expansion table (it shares the 1-bit `PlanarBitPack` decode).

Everything else in the table is still on the unscheduled float path
(0.01x-0.12x) and untouched. All 28 roundtrip tests pass; `kernel-bench --all`
reports no mismatches; odd block-count tails are correct (verified at n = 32,
96, 160, 224, 1056).

Commits, oldest first:

- `c66ab336d` apps/ggml: bring q4_0/q8_0 vec_dot up to ggml-cpu speed
- `c8ec1934d` Add `Stage::distribute()`, and one accumulator per term in
  `hoist_invariants()`
- `6c72fb4c1` apps/ggml: take the affine vec_dots (q4_1, q5_1) to SDOT
- `a6e9b0962` `hoist_invariants()`: return one Func per accumulator, not a Tuple

## Dev aids

The `getenv("GGML_PER_BLOCK_PROBE")` branch (original "variant A" via
`sdot_partial`) is kept, now reachable only for the *symmetric* SDOT formats
(q4_0/q8_0/q5_0) -- a dev aid to measure per-block vs the lane-split default.
The affine formats reach `sever_sum` first and never see it.

Note the generator reads the env var at *generator run time*, so changing it
does not invalidate ninja's outputs. Force regeneration:

```sh
rm -f build/apps/ggml/halide/q4_0_vec_dot.o build/apps/ggml/halide/libq4_0_vec_dot.a
GGML_PER_BLOCK_PROBE=1 cmake --build build/apps/ggml -j --target q4_0_vec_dot
cmake --build build/apps/ggml -j
```

## Building

libHalide is consumed from `install/macOS`, so an app change needs only the app
build, but a Halide change needs build + install first:

```sh
cmake --build build/macOS -j --target Halide
cmake --install build/macOS --prefix install/macOS
cmake --build build/apps/ggml -j
```

`build/macOS` is configured with `WITH_TESTS=OFF`, so `correctness_*` targets do
not exist. Compile a test directly instead:

```sh
c++ -O1 -std=c++17 -DHALIDE_KEEP_MACROS -DHALIDE_WITH_EXCEPTIONS \
	-I install/macOS/include -I test/common -I tools \
	test/correctness/rfactor.cpp \
	-L install/macOS/lib -lHalide -Wl,-rpath,$PWD/install/macOS/lib -o /tmp/rfactor && /tmp/rfactor
```

`HALIDE_KEEP_MACROS` is required (`internal_assert` is `#undef`'d at the end of
the installed `Halide.h`); `HALIDE_WITH_EXCEPTIONS` is required or the
exception-guarded tests silently do not compile. To find which sub-test fails,
shard it: `TEST_TOTAL_SHARDS=40 TEST_SHARD_INDEX=N ./rfactor`.

## Measuring

macOS moves the process between P- and E-cores between runs, so a single run's
absolute numbers are not comparable -- swings of 30%+ are normal. Take the best
of several runs and always read the halide/ggml-cpu *ratio* within a run.

`src/bench_vecdot.cpp` has two dev aids added this session:
`KERNEL_BENCH_FILTER=q4_0,q8_0` (substring match on type name) and
`KERNEL_BENCH_N=<n>` (vector length; default 4096). The n sweep is what
separates per-call overhead from per-block cost -- fit the slope.

Repeat-and-take-best wrapper:

```sh
#!/bin/zsh
B=~/dev/Halide/ggml-on-qk/build/apps/ggml
N=${N:-5}
FILTER=${FILTER:-q4_0,q4_1,q8_0}
TMP=$(mktemp -d)
for i in $(seq $N); do KERNEL_BENCH_FILTER=$FILTER $B/kernel-bench --vecdot --csv $TMP/r$i.csv >/dev/null; done
cat $TMP/*.csv | awk -F, '
  $3!="role" && $1=="vec_dot" { k=$2 SUBSEP $4; if (!(k in best) || $5+0 < best[k]) best[k]=$5+0; ok[k]=$9; types[$2]=1 }
  END { n=0; for (t in types) st[++n]=t
        for(a=1;a<n;a++)for(b=a+1;b<=n;b++)if(st[a]>st[b]){tmp=st[a];st[a]=st[b];st[b]=tmp}
        for (i=1;i<=n;i++) { t=st[i]; c=best[t,"ggml-cpu"]; h=best[t,"halide"]
          printf "%-8s %9.1f ns %10.1f ns %7.2fx  %s\n", t, c, h, (h>0?c/h:0), (ok[t,"halide"]==1?"yes":"NO") } }'
rm -rf $TMP
```

To read generated code, re-run the generator by hand with extra outputs. Grab
the exact command from `build.ninja` (`grep 'COMMAND = .*-n q4_1_vec_dot '`) and
swap `-e c_header,object` for `-e stmt,assembly`. Add
`-no_asserts-no_bounds_query` to the target to see what actually ships.

Diagnose SDOT vs fallback by grepping the `.s` for `sdot.4s`; grep the `.stmt`
for `vector_reduce_add(int32x..(widening_mul(int8x.., int8x..)))`.

## What the q4_0/q8_0 speedup actually was

Four independent things, roughly equal in size:

1. **Lanes from `r.x`, not `r.y`.** `rfactor({{rxo, lane}, {r.y, u}})` keeps the
   sdot's four Int(32) lanes alive into the float accumulator, so no block pays
   a horizontal reduce. Lanes must come from `r.x`: blocks are interleaved
   `{scale, codes}` records, so a lane per block gathers both the codes and the
   scales.
2. **Chained sdot.** Cut `r.x` into chunks of 16 run serially, so both sdots
   accumulate into the *same* register. Reducing straight to 4 lanes makes
   `CodeGen_ARM` lower the wide reduce as two independent sdots plus an `addp`
   (`codegen_dot_product_vector_reduce` only matches factor 4 and recurses).
3. **Interleave 4 blocks into independent accumulators.** Widening the vector
   does not help -- every lane of one accumulator advances on every block, so
   only interleaving blocks shortens the multiply-add chain. Un-interleaved, the
   kernel is latency-bound at ~4 cycles/block.
4. **Per-call overhead.** vec_dot is called once per output element of a matvec,
   so nothing amortizes. Three `Halide::Runtime::Buffer` constructions cost ~13
   ns flat; the assert/bounds-query prologue another ~10 ns. Fixed by filling a
   `halide_buffer_t` in place (`StackBuffer` in `ggml_quants.cpp`) and building
   these libraries with `FEATURES no_asserts no_bounds_query`.

Two traps found along the way:

- **A predicated tail is not a local cost.** Splitting the block RVar with
  `GuardWithIf` makes the per-block sdot a dynamic-extent allocation that Halide
  has to `bzero` and accumulate *through memory*, roughly doubling the cost of
  every block. Fixed by giving the main reduction an exactly divisible extent
  (`(nb / unroll_blocks) * unroll_blocks`) and sweeping the remainder in a
  second update at the default schedule. `unroll_blocks` must be a power of two
  -- 3 and 6 measured 40% worse because the simplifier cannot discharge the
  tail.
- **`specialize()` inherits the schedule as of the call**, so scheduling
  directives applied *after* `specialize()` do not reach the specialized branch.
  It silently dropped the vectorize/unroll and made things 4x slower.

## The q4_1 story so far

q4_1 is affine: the weight decodes to `d*code + m`, so the per-block product
`(d*code + m) * (d_act*act)` has no single scale to hoist and it was left on the
default (fully scalar, unscheduled) float reduction at 3843 ns.

`Stage::distribute()` multiplies the product out to
`d*d_act * sum(code*act) + m*d_act * sum(act)`, and `hoist_invariants()` gives
each term its own accumulator. Both bodies are integer, so both reach SDOT --
the first as the ordinary dot, the second as a dot with a vector of ones (ARM
already matches `i32(int8x)`). That is ggml's own decomposition, and it got q4_1
to 133.8 ns / 0.80x.

**Design decisions worth not re-litigating:**

- Multiplying out is `distribute()`, a separate schedule directive, *not*
  something `hoist_invariants()` decides. A first attempt did it unconditionally
  and broke `hoist_invariants test (predicated RDom)`: `require(...) * (r + 1)`
  distributes into two accumulators when one was optimal. The predicate that
  would have rescued it ("ignore constants, don't traverse call arguments...")
  is exactly the kind of heuristic that needs tuning forever. Whether to
  multiply out depends on what the terms turn out to contain, which is a cost
  question, so it belongs in the schedule.
- `hoist_invariants()` returns **one single-valued Func per accumulator**, not a
  Tuple. The Tuple version worked and measured identically, but it blocked
  `compute_offline` (which cannot sever one value of a Tuple) and forced
  `change_type` to grow multi-output support. Fusing the terms' loop nests is
  `compute_with`'s job.

## The stored block sum -- DONE (q4_1 0.97x)

ggml does *not* recompute `sum(act)` at vec_dot time -- it reads the `s` field
that `block_q8_1` stores at quantize time
(`{ggml_half d; ggml_half s; int8_t qs[32]}`, 36 bytes). Our Q8_1 codec already
computes that field (`AppendSums{block_size, SumMode::ScaledFloat}` in
`make_symmetric_byte_sum_block_scheme`). We now sever the offset term's
accumulator straight to it via `compute_offline`, which needs no new Halide
directive: its contract already *is* this claim -- sever a Func's computation,
replace calls with an ImageParam read, discard the recomputing reduction.

**Result: variant A + sever measured 110.2 ns / 0.97x** (q5_1: 208 ns / 0.64x).
This *matched* the lane-split+sever projection, so the second route below
(teaching `distribute()` to split into separate update definitions for per-term
rfactors) is **not needed** -- skip it. All correct: 28 roundtrips,
`kernel-bench --all` clean, odd tails at n = 32/96/160/224/1056.

The recipe, as implemented in `vec_dot_generator_base.h`'s `sever_sum` branch
(reached when `distribute_terms && act_has_block_sums`, i.e. affine x Q8_1):

1. `acc_dot = Acc.update().rfactor({{r.y, u}})` -- whole-block partials (variant
   A). Inline **only the weight's** decode chain (replacement + inlinable
   handles, multi-pass), leaving the activation decode `Act`
   (`act_r.replacement`) whole. `distribute()`, then `hoist_invariants()`. The
   offset term's accumulator body is then `Act(r.x, u)`, so the accumulator *is*
   `sum_k decode_act(k, blk)` = the stored `s`.
2. `parts[1].change_type(Float(16))` -- makes the severed Func's type match the
   data. Faithful: the encoder rounds `s` to fp16 too (the ~6e-06 rel err is
   exactly that rounding, same as ggml's).
3. `Pipeline({Acc}).compute_offline({s16}, {s_blocks})` -- second
   `compute_offline` on the pipeline (the first, at configure top, severs the
   encode halves to x_blocks/y_blocks). `s_blocks` is the third Input.
4. Product term: inline `Act`'s **full** chain into `parts[0]` (one Act
   eager_inline is not enough -- the chain has intermediate levels, and a single
   inline leaves the second hoist with no visible d_act factor and it errors),
   re-`hoist_invariants()` to pull d_act out, `change_type(Int(32))` -- the
   survivor reaches SDOT. Verified: 8 `sdot` in the `.s`; writeback is
   `d_w*(d_act*int32_dot) + m_w*s_blocks[blk]`, ggml's exact decomposition, with
   no `sum(act)` reduction left anywhere in the stmt.

Plumbing (all landed, see Uncommitted):

- `s_blocks`: 1-D `Float(16)` Input, `dim(0).set_stride(act_bytes/2)` (= 18 for
  Q8_1) -- pinning it makes the read an immediate offset, *and* is required:
  left dynamic, Halide's default constrains the innermost stride to 1 and the
  bound-check fails against the strided view.
- ABI: `StackBuffer::blocks_field_f16(base, nb, byte_offset=2, block_bytes=36)`
  -- a zero-copy fp16 view of the `s` slot, stride `block_bytes/2`.
- Format knowledge lives with the codec: `SchemeAndBytes::has_block_sums` set by
  `make_symmetric_byte_sum_block_scheme`, carried to
  `VecDotSpec::act_has_block_sums` (guarded `&& a_nat == wbs`, so a Reblock'd
  activation stays off it).

**The structural conflict (why variant A, not lane-split).** The two terms want
different rfactors: `sum(code*act)` wants `{rxo->lane, r.y->u}` (the lane split
is the q4_0/q8_0 win); `sum(act)` must be the *whole-block* sum to equal `s`, so
`{r.y->u}` only. One update rfactors one way. Variant A gives both `{r.y->u}`;
severing then deletes the offset accumulator entirely, so its per-lane-partial
problem never arises -- and the surviving product dot, alone in its block loop,
schedules close enough to the lane-split base that the ~8 ns gap projected
between the routes did not materialize. `compute_with` (verified to fuse a
lane-split with a block-only reduction using `AlignStart` + matching split-var
names) is therefore unnecessary here; keep it in mind for formats that keep two
live accumulators.

## The q5_x 5-bit high bit -- DONE (q5_0 0.94x, q5_1 0.93x)

q5_0/q5_1 reach SDOT, but every code carries a per-element high bit unpacked
from the `qh` field, and that reconstruction -- not the dot, not (for q5_1) the
offset -- is the whole gap vs q4_x.

**What the reconstruction was.** `PlanarBitPack::decode`'s 1-bit case emitted
`(qh[kk/8] >> (kk%8)) & 1`: a per-lane variable shift plus a `transpose_vector`
to broadcast the two `qh` bytes across the 16 sdot lanes
(`dup.8b + dup.4h + uzp2` per sdot, ~24 NEON ops/block). ggml avoids this with a
1 KB `table_b2b` memory LUT (byte -> 8 expanded bytes, one contiguous load per
`qh` byte).

**What was done.** Mirrored the LUT: compile-time b2b tables embedded in the
binary. The q5 tables contain the final high-bit contribution (`-16/0` for q5_0,
`0/16` for q5_1), so the lookup folds the shift and q5_0 zero-point into the
load rather than reconstructing a raw bit and applying them afterward. Several
other details are necessary:

1. The table read is only a *contiguous* 8-byte load
   (`b0[ramp(qh_byte*8, 1, 8)]`, matching ggml) when the `qh` byte is a
   **scalar** and the 8 bit positions are the vector lanes. Inlined into the
   sdot it is the opposite (the byte varies per lane -> a 16-wide per-lane
   gather, `ld1` per lane, measured 0.12x). So the reconstructed codes are
   **materialized** per block (`combine_bits_code` compute_at the block loop,
   `kk` split `(byte, pos)`: pos vectorizes the load, byte unrolls to a scalar
   index). To keep the codes in the vector register file rather than round-trip
   a stack buffer, the materialization is `store_in(MemoryType::Register)` with
   `kk` split further into 16-code units (one sdot chunk = two `qh` bytes x 8
   positions) so the store width matches the sdot's read width -- otherwise the
   8-wide table-load store vs 16-wide sdot load mismatch keeps it in memory.
2. Materializing needs the codes leaf kept out of `sdot_partial()`'s deep inline
   (`can_be_inlined()` ignores compute level, so a `compute_root`/`compute_at`
   schedule alone does not stop `eager_inline` -- a `keep_out` name list does).
3. `block_q5_0` and `block_q5_1` are represented as packed Halide struct types,
   including `qh` as a scalar `UInt(32)`. `LowerStructTypes` now preserves a
   multi-byte packed field read as `concat_bits()` of adjacent bytes instead of
   immediately expanding it into a shift/or tree. LLVM can consequently issue
   one unaligned `ldr/ldur w`, matching ggml, rather than four `ldrb`s. A
   correctness/codegen regression test covers the deliberately unaligned qh
   field at byte offset 2.
4. The odd-block **tail** reads the same codes but is a separate update
   `compute_at` cannot reach. Fixed by giving the tail its **own** decode chain:
   a second `Wt/Vec` placeholder pair, its own `approximate_by`, both bound to
   the same `x_blocks/y_blocks`. Main materializes; the tail reconstructs inline
   (< `unroll_blocks` blocks, negligible). This is also what unlocks the
   per-block fusion -- with a shared chain Halide hoists the codes buffer to
   whole-row.

The transpose is gone (verified: no `uzp2`/`dup.8b`); reconstruction is a
handful of ops + 4 contiguous LUT loads/block. Bonus: the same decode change
sped up q5_K on the float path (8615 -> 6730 ns).

**The schedule changes still matter after the compiler fix.** The compiler
change is the dominant improvement, but it does not make the schedule work
redundant:

- Removing the explicit qh `compute_at(...).store_in(Register)` schedule made
  q5_0 regress from about 122 to 129 ns and q5_1 from about 152 to 162 ns, even
  though both generated versions contained a word load. Keep it: it changes
  placement/reuse around the four bit extracts, not merely the load width.
- Interleaving two q5 blocks gives better latency hiding/register pressure than
  the four-block setting used by q4/q8. Applying two globally regressed q4_0 and
  q4_1, so this is a per-format `VecDotSpec` choice.
- Explicitly unrolling the fixed-size final rfactor reductions removes a stack
  spill and one-iteration epilogue loops (roughly another 1 ns here).

**q5_1 needs a different reduction shape.** The generic affine path horizontally
reduced each block's Int32 dot before accumulating floats. The q5_1-specific
schedule keeps four float dot lanes live across the entire block loop, maintains
two scalar `m*s` accumulators alongside them, and fuses their two-block loops.
The generated steady state now matches the important shape of ggml's kernel: two
SDOTs per block, persistent vector FMAs, and scalar offset FMAs, with one
horizontal reduction at the end.

One representative paired n=4096 run after the final epilogue change was q5_0
122.2 ns ggml / 130.2 ns Halide (0.94x), and q5_1 143.5 / 153.7 ns (0.93x). Core
migration moves both the absolute numbers and ratios; another run measured 121.8
/ 121.8 and 143.2 / 144.3 before the final ~1 ns epilogue improvement. Use
repeated paired runs rather than treating either sample as a fixed score.

**Build-flag bug.** q5_0/q5_1's `add_halide_library` were missing
`FEATURES no_asserts no_bounds_query` (every other tuned vec_dot has it), so
they paid the assert/bounds-query prologue -- ~11 ns of pure startup on a ~170
ns call. Adding it was worth 0.63 -> 0.67x on its own; the register store
another 0.67 -> 0.69x (q5_1 to 0.78x). Check this first on any new kernel.

Group-level codes compute (all blocks first) measured worse than the final
per-block materialization. Likewise, removing qh materialization because the new
compiler lowering already produced `ldr w` was a measured regression; the two
optimizations address different parts of the generated loop.

## Not started

Everything else (k-quants, IQ family, tq\*) is still on the default unscheduled
float reduction and would benefit from the same treatment as q4_1 -- the
k-quants' two-level scales are also sums of scaled sub-reductions, which is what
`distribute()` was built for.
