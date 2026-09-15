# 11. Pitfalls

These are the traps that come up over and over. Most are just corollaries of
Part III, hit under performance pressure.

## The parallel loop has to be outermost

`parallel(var)` doesn't lift `var` to the outermost spot. It marks that loop
parallel in place. Any serial loop already outside `var` runs serially, and
launches a fresh parallel-for on each of its iterations, which is Nx dispatch
overhead.

An easy way to check: after all the `split`/`reorder`/`fuse` calls, the parallel
var should be the last argument of the final `reorder` (reorder is
innermost-first, so the last arg is outermost), or have nothing outside it.

Here's the classic wrong version:

```cpp
consumer.split(x, xo, xi, 64).split(y, yo, yi, 32)
        .reorder(xi, yi, c, xo, yo)   // last arg yo is OUTERMOST
        .parallel(xo);                 // BUG: xo isn't outermost, yo is
```

This launches the thread pool once per `yo` iteration. The fix is to make the
parallel var last, or to fuse the two outer axes into one parallel loop:

```cpp
consumer.split(x, xo, xi, 64).split(y, yo, yi, 32)
        .reorder(xi, yi, c, yo, xo)
        .fuse(yo, xo, t)               // must be adjacent in the nest
        .parallel(t);
```

Profiler signature: the Func's `parallel loops` count comes out above 1.

## `compute_at` recompute multipliers

Every axis outside `compute_at(consumer, var)` acts as a recompute multiplier.
With `producer.compute_at(consumer, var)`, the producer gets re-evaluated at
every iteration of every consumer loop outside `var`. So if the consumer's outer
nest is `yo, xo, c, yi, xi` and the placement is `compute_at(consumer, xo)`, the
producer gets recomputed per `(yo, xo)` pair.

Profiler signature: `recompute ratio > 1` on the producer, often alongside
`heap allocs` proportional to the multiplier.

A good way to spot-check any `compute_at` is to ask what axes sit outside `var`,
and whether each one adds to the producer's needed range. The ones that do mean
recompute; the ones that don't are free. Common fixes are to avoid splitting the
outer axis, move `compute_at` outside the multiplying axis, use `hoist_storage`
to at least save the allocation, or `fuse` the multiplier into the parallel var.

## Expensive producers inside RDoms

This is a special case of the multiplier above. In an update
`out(x, y) += f(g(x, y, r), ...)` where `r` is a search or blur extent (an
nl-means search area, bilateral-grid weights), a producer P that isn't indexed
by `r` but sits `compute_at` inside the reduction gets recomputed once per
reduction step. That works out to `P_cost × |r|` work.

Profiler signature: P's `recompute ratio ≈ |r|`, say 49 for a 7x7 search.

The fix is to place P at a loop level outside the reduction, at the same tile
level as the consumer, and combine it with `hoist_storage` to keep one
allocation reused across inner iterations.

## True axis-level recurrences

A true axis-level recurrence is when the update at position `k` along an axis
reads from position `k-1` (or earlier) along that same axis: IIR filters,
summed-area tables, prefix scans. The recurrence axis can't be parallelized,
since each step reads the previous step's output.

It helps to know what isn't a true recurrence, because these axes stay
parallelizable:

- **Staged reductions** where a small bookkeeping dim recurs but the spatial
  dims don't. A log-height max filter is the usual example: each slice reads the
  previous slice, but the x/y rows within a slice are independent. Only the
  small bookkeeping dim is serial.
- **Associative RDom reductions** (`sum`, `maximum`). The accumulator dim is
  serial by default but `rfactor`-able, and the spatial axes are free.

There's one rule that covers both. If a hot producer has any axis it can't be
parallelized over (a true recurrence, a staged-reduction bookkeeping dim,
whatever the reason), pick the consumer's outer parallel axis from the
producer's parallelizable axes. Then each consumer-parallel task owns a complete
slab of the producer along its serial axis, and the producer can sit
`compute_at` inside the consumer's parallel loop with no redundancy.

For example, take `vert_log(x, y, c, t)` where `t` is a staged-reduction dim
(each slice depends on the previous) but `x, y, c` are all parallelizable.
Parallelizing the consumer over fused `(xo, c)` and putting
`vert_log.compute_at(consumer, that_axis)` gives each task the full-`y`,
full-`t` slab for one (x-strip, channel) pair, and the `t`-recurrence runs
serially inside the task.

Profiler signature on a violation: the producer forced into its own parallel
region (`parallel loops > 1`) with `recompute ratio > 1`. Or, if it's worked
around with `compute_root`, a high `peak heap` and a single-threaded `free`.

## Common mistakes catalog

A grab-bag of smaller traps, most of them corollaries of the rules above:

- **Vectorize factor bigger than the inner extent.** Produces scalar tail code.
  Keep the factor at or under the natural vector width, and `bound()` if needed.
- **Vectorizing a tiny fixed axis like `c`** (2, 3, 4). Width-2 SIMD is worse
  than scalar, so `bound(c, 0, N).unroll(c)` and vectorize the large stride-1
  axis instead.
- **Parallel over too small an axis.** `parallel(c)` on 3 channels gives 3 tasks
  on a 64-core machine. Fuse with a larger axis, or pick another.
- **Forgetting `bound` on small dims.** Without it, Halide can't fully unroll
  `c`.
- **Using `unroll` when `vectorize` was meant.** Unroll keeps the loops scalar;
  vectorize uses SIMD.
- **Scheduling update stages like the pure stage.** `f.parallel(y)` and
  `f.vectorize(x)` only schedule the pure def. Each update needs its own
  `f.update(i).parallel(y)`.
- **Computing a shared producer inside one consumer's loop.** It gets recomputed
  per consumer. A `compute_root`, or a shared `compute_at` above all consumers,
  is almost always better.
- **No boundary condition plus the default tail strategy.** This gives
  out-of-bounds reads, or slow tail code. Add a `BoundaryConditions` wrapper on
  inputs read with offsets.
- **Thinking `reorder(a, b)` puts `a` outermost.** It doesn't. The args are
  innermost-first, and the last one is outermost.
- **Vectorizing a scatter** (a data-dependent index into the Func being
  written). This is almost always a mistake (see [Recipes](10-recipes.md)).
- **Scheduling a trivial pure def on its own**, like `f(...) = 0.0f` followed by
  a meaningful update. Schedule the Func once, with pure and update at the same
  site.
- **Blindly using `vec = natural_vector_size<float>()`.** Great for full-image
  passes, but for small Funcs (a 192-wide grid, a 12-deep axis), width 8 or
  unrolling can do better.
