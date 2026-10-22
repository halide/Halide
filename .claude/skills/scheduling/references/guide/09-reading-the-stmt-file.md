# 9. Reading the `.stmt` File

The profiler catches most problems, so the lowered `.stmt` file (or
`print_loop_nest()`) is mainly for one thing it doesn't show directly:
**vectorization shape**. The question is whether the hot loops are dense
unit-stride SIMD, or hidden scatters and gathers. It's most useful when a Func
is hot but well-parallelized, or to confirm a specific `compute_at` level took
effect.

## What the lowered IR looks like

Vectorized and parallel loops don't survive lowering as keyword-tagged `for`s:

- **Parallel loops** turn into `halide_do_par_for(<closure>, 0, <extent>, ...)`
  calls, with a matching `internal func ..._par_for_...` block earlier in the
  file. If `parallel()` was called and there's no matching `halide_do_par_for`,
  it didn't take.
- **Vectorized inner loops** disappear entirely. The body's scalar ops become
  vector ops.

A few vector op shapes are worth recognizing:

- **Vector store:** `buf[ramp(<base>, <stride>, <lanes>)] = <vec expr>`.
  Unit-stride (`stride == 1`) is the goal.
- **Vector load:** the same shape on the right-hand side.
- **Broadcast:** `xN(<scalar>)`, where `N` is the lane count.
- **Allocation:** `allocate <Func>[...] stack`, or `allocate <Func> ...`.
- **Sliding window:** the allocation has a power-of-2 size in the slid
  dimension, and loads use `%` or bitwise AND in the index.

## The vectorization checklist

For every Func's hot loop, it's worth asking:

1. **Is it vectorized at all?** Stores should look like
   `buf[ramp(base, 1, N)] = ...`. A scalar `buf[scalar_idx] = ...` means the
   inner axis didn't get vectorized. A common trap here is update stages:
   `f.vectorize(x, vec)` only schedules the pure definition, so each update
   needs its own `f.update().vectorize(x, vec)`. One scalar stage can tank the
   whole pipeline.
2. **Are the vectors wide enough?** `N` should equal `natural_vector_size<T>()`:
   16 for float, 32 for `uint16`/`int16` on AVX-512. A smaller `N` means the
   loop runs at half or quarter machine width.
3. **Any avoidable scatters?** Stores should be unit-stride ramps. A
   `ramp(base, stride≠1, N)` is really N serial scalar stores, and it usually
   means the vectorized axis isn't this Func's stride-1 storage axis. A
   `buf[index_vec] = ...` is a data-dependent scatter (see
   [Recipes](10-recipes.md), histograms).
4. **Any avoidable gathers?** Loads should be unit-stride ramps or broadcasts. A
   `ramp(base, stride≠1, N)` is a strided gather, so check whether the
   producer's stride-1 storage axis matches the consumer's vectorized axis, and
   fix it with `reorder_storage` if not. A `f(x*2, y)` is a genuine stride-2
   gather, where it can help to compute the producer at a coarser granularity so
   neighboring output vectors share loads.

What to aim for: every stage vectorizes its own stride-1 storage axis at
`natural_vector_size<T>()` width, and every hot-loop store and load is a
unit-stride ramp or a broadcast.
