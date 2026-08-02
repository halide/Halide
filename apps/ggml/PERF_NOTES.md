# vec_dot performance notes

Working notes for bringing `apps/ggml`'s vec_dot kernels up to `ggml-cpu` speed
on ARM (measured on an M3 Max). Branch `alexreinking/ggml-on-qk`, worktree
`~/dev/Halide/ggml-on-qk`.

## Where things stand

Measured at n=4096, best of many runs (see "Measuring" below):

| type | ggml-cpu | halide   | ratio | at session start |
| ---- | -------- | -------- | ----- | ---------------- |
| q4_0 | 94.2 ns  | 97.3 ns  | 0.97x | 0.52x            |
| q4_1 | 106.7 ns | 133.8 ns | 0.80x | 0.03x            |
| q5_0 | 122.2 ns | 241.7 ns | 0.51x | 0.38x            |
| q5_1 | 143.4 ns | 288.1 ns | 0.50x | 0.04x            |
| q8_0 | 71.9 ns  | 75.6 ns  | 0.95x | 0.61x            |

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

## Uncommitted

`apps/ggml/halide/vec_dot_generator_base.h` has a probe branch guarded by
`getenv("GGML_PER_BLOCK_PROBE")` -- "variant A", the per-block (single rfactor)
schedule. It exists only to measure; delete it or keep it as the base for the
next step (see below). The default path is unchanged and measures as above.

Note the generator reads the env var at *generator run time*, so changing it
does not invalidate ninja's outputs. Force regeneration:

```sh
rm -f build/apps/ggml/halide/q4_1_vec_dot.o build/apps/ggml/halide/libq4_1_vec_dot.a
GGML_PER_BLOCK_PROBE=1 cmake --build build/apps/ggml -j --target q4_1_vec_dot
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
  (`(nb / kUnrollBlocks) * kUnrollBlocks`) and sweeping the remainder in a
  second update at the default schedule. `kUnrollBlocks` must be a power of two
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

## Next step: the stored block sum

ggml does *not* recompute `sum(act)` at vec_dot time -- it reads the `s` field
that `block_q8_1` stores at quantize time
(`{ggml_half d; ggml_half s; int8_t qs[32]}`, 36 bytes). Our Q8_1 codec already
computes that field (`AppendSums{block_size, SumMode::ScaledFloat}` in
`make_symmetric_byte_sum_block_scheme`).

**This needs no new Halide directive.** `compute_offline`'s contract already
*is* this claim: it severs a Func's computation, replaces calls with an
ImageParam read, and hands back an `offline` Pipeline that computes exactly
those values -- which for Q8_1 is the quantizer that writes `s`. Same claim
already made for the packed codes. Proven end to end in a standalone probe:

```
accumulators: 2
inner accumulator type: int32
reference = -1535.121338
severed   = -1535.112305  (rel err 5.88e-06)
```

The recipe that works:

1. Leave the **activation decode un-inlined** through `distribute()` and the
   first hoist, so the offset term's accumulator body is exactly `Act(r.x, u)`
   and the accumulator *is* `sum_k decode_act(k, blk)`. (`sdot_partial()`
   currently eager-inlines both operands' whole decode chains; for this path it
   must inline the weight's only.)
2. `parts[1].change_type(Float(16))` -- the stored field is fp16, and this is
   what makes the severed Func's type match the data. Faithful: the encoder
   rounds to fp16 too. The 5.9e-06 error above is that rounding, same as ggml's.
3. `Pipeline({Acc}).compute_offline({sum16}, {stored_param})`.
4. `parts[0].update().eager_inline({Act})` then hoist again to pull the
   activation scale out, then `change_type(Int(32))` -- the survivor still
   reaches SDOT.

**The structural conflict.** The two terms need different rfactors:

| term            | preserved dims        | why                                                                                 |
| --------------- | --------------------- | ----------------------------------------------------------------------------------- |
| `sum(code*act)` | `{rxo->lane, r.y->u}` | the lane split is the q4_0/q8_0 win                                                 |
| `sum(act)`      | `{r.y->u}` only       | must be the *whole-block* sum to equal `s`; lane-split gives four per-lane partials |

One update definition can only be rfactored one way, and `distribute()` +
`hoist_invariants()` produce two accumulator Funcs from a *single* update, so
both inherit its rfactor.

**Measured cost of each way out** (this is the important part -- an earlier
estimate that this was net-zero was wrong):

| structure             | q4_0 (1 accumulator) | q4_1 (2 accumulators) | cost of the 2nd |
| --------------------- | -------------------- | --------------------- | --------------- |
| lane-split (current)  | 97.3 ns              | 133.8 ns              | 36.5 ns         |
| per-block (variant A) | 105.7 ns             | 169.9 ns              | 64.2 ns         |

The per-block structure costs only **+8.4 ns** on the base (97.3 -> 105.7). Most
of the apparent 30.8 ns q4_1 penalty is the second accumulator getting more
expensive -- exactly what severing removes. ggml's own q4_1 - q4_0 delta is
**12.6 ns**, which is what a well-scheduled stored-`s` offset term costs.

So:

| route              | projection                 | ratio  | machinery                            |
| ------------------ | -------------------------- | ------ | ------------------------------------ |
| today              | 133.8 ns                   | 0.80x  | --                                   |
| variant A + sever  | ~105.7 + 12.6 = **118 ns** | ~0.90x | severing plumbing only               |
| lane-split + sever | ~97.3 + 12.6 = **110 ns**  | ~0.97x | + distribute-into-update-definitions |

`compute_with` **is** verified to fuse a lane-split reduction with a block-only
one over the same block loop (probe: one block-group loop, correct result),
using `LoopAlignStrategy::AlignStart` and *matching split-variable names* -- the
loop variables must be named identically in both stages or it errors with
"cannot find <var> in <stage>".

**Recommended order:** build the severing plumbing on variant A first (it is
shared by both routes), measure the real number, then decide whether the last ~8
ns justifies teaching `distribute()` to split into separate update definitions
so each term can be rfactored on its own.

Plumbing still to write:

- A third generator input: 1-D `Float(16)` ImageParam for the block sums.
- The ABI wrapper passes a zero-copy view of the `s` field: `host = base + 2`,
  1-D, extent `nb`, **stride 18 in fp16 units** (= 36 bytes). `StackBuffer` in
  `ggml_quants.cpp` already builds `halide_buffer_t`s in place; add a
  `blocks_field_f16` helper.
- Where the "this activation stores its block sums" knowledge lives. It is
  format knowledge, so it belongs with the codec --
  `make_symmetric_byte_sum_block_scheme` is the Approximation construction site,
  and `SchemeAndBytes`/`VecDotSpec` is the natural carrier. Do not put it in the
  generator's schedule block.

## Not started

q5_0 (0.51x) and q5_1 (0.50x) are on the SDOT path but well behind; the 5-bit
high bit comes from a separate 4-byte field and the reconstruction may not be
folding into the codes leaf as cleanly as q4_0's nibble unpack. Everything else
(k-quants, IQ family, tq\*) is still on the default unscheduled float reduction
and would benefit from the same treatment as q4_1 -- the k-quants' two-level
scales are also sums of scaled sub-reductions, which is what `distribute()` was
built for.
