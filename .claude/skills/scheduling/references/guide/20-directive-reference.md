# 20. Directive Reference

A quick index of the scheduling directives. Each one is a method of `Func` (or,
for update stages, `Func::update(int)`), and the chapter link leads to the full
story.

## Where to compute and store

| Directive                       | Effect                                                                                                                                         |
| ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `f.compute_root()`              | Compute all of `f` once, before any consumer. ([Ch. 14](14-placement-compute-root-and-compute-at.md))                                          |
| `f.compute_at(g, var)`          | Compute just enough of `f` inside `g`'s loop over `var`, per iteration. ([Ch. 14](14-placement-compute-root-and-compute-at.md))                |
| `f.compute_inline()`            | Reset to the inline default; undoes a previous `compute_*`. ([Ch. 13](13-defaults-and-inlining.md))                                            |
| `f.store_at(g, var)`            | Allocate `f`'s storage at `g`'s `var` loop (coarser than compute enables a sliding window). ([Ch. 15](15-storage-levels.md))                   |
| `f.store_root()`                | Allocate `f`'s storage at the outermost level. ([Ch. 15](15-storage-levels.md))                                                                |
| `f.hoist_storage(g, var)`       | Hoist just the *allocation* to `g`'s `var` loop; keep the compute schedule. Never hoist past a parallel loop. ([Ch. 15](15-storage-levels.md)) |
| `f.fold_storage(var, K)`        | Circular buffer of `K` slots in `var`, for sliding windows. ([Ch. 15](15-storage-levels.md), [Ch. 10](10-recipes.md))                          |
| `f.store_in(MemoryType::Stack)` | Allocate on the stack; small fixed sizes only. ([Ch. 15](15-storage-levels.md))                                                                |

## Loop shape

| Directive                              | Effect                                                                                                      |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `f.split(x, xo, xi, factor)`           | Split `x` into outer `xo` and inner `xi` (`x = xo*factor + xi`). ([Ch. 16](16-reshaping-loops.md))          |
| `f.fuse(a, b, t)`                      | Collapse two **adjacent** loops into one `t`. ([Ch. 16](16-reshaping-loops.md))                             |
| `f.tile(x, y, xo, yo, xi, yi, tx, ty)` | Two splits plus a reorder into tiled traversal. ([Ch. 16](16-reshaping-loops.md))                           |
| `f.reorder(a, b, c, …)`                | Reorder loops, **innermost first** (last arg = outermost). ([Ch. 16](16-reshaping-loops.md))                |
| `f.reorder_storage(a, b, c, …)`        | Change memory layout, innermost-dim first. ([Ch. 16](16-reshaping-loops.md), [Ch. 10](10-recipes.md))       |
| `f.bound(x, min, extent)`              | Promise `x`'s range at compile time; enables fixed-size unroll/vectorize. ([Ch. 16](16-reshaping-loops.md)) |

## Execution and loop types

| Directive                                        | Effect                                                                                                                   |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| `f.parallel(var)`                                | Run iterations of `var` on the thread pool. ([Ch. 17](17-loop-types.md))                                                 |
| `f.parallel(var, task_size)`                     | Block `var` by `task_size` first (fewer, larger tasks). ([Ch. 17](17-loop-types.md), [Ch. 5](05-scheduling-for-cpus.md)) |
| `f.vectorize(var[, factor])`                     | Emit SIMD; prefer `factor = natural_vector_size<T>()`. ([Ch. 17](17-loop-types.md))                                      |
| `f.unroll(var[, factor])`                        | Unroll the loop (stays scalar). ([Ch. 17](17-loop-types.md))                                                             |
| `f.serial(var)`                                  | Reset a dimension to serial. ([Ch. 17](17-loop-types.md))                                                                |
| `f.gpu_blocks/threads/lanes(…)`, `f.gpu_tile(…)` | Map dimensions to GPU blocks/threads/lanes. ([Ch. 17](17-loop-types.md))                                                 |

## Wrappers, reductions, and variants

| Directive                    | Effect                                                                                                 |
| ---------------------------- | ------------------------------------------------------------------------------------------------------ |
| `f.in(g)`                    | Introduce an identity wrapper of `f` for consumer(s) `g`. ([Ch. 18](18-advanced-directives.md))        |
| `f.clone_in(g)`              | A fresh recomputed copy of `f` for `g`, independently scheduled. ([Ch. 18](18-advanced-directives.md)) |
| `f.update(n).rfactor(rv, v)` | Factor a reduction so the preserved axis becomes parallelizable. ([Ch. 18](18-advanced-directives.md)) |
| `b.compute_with(a, var)`     | Fuse two stages into one shared loop nest down to `var`. ([Ch. 18](18-advanced-directives.md))         |
| `f.specialize(cond)`         | Conditional schedule variant selected at run time. ([Ch. 18](18-advanced-directives.md))               |
| `f.specialize_fail(msg)`     | Make the fallback a run-time error. ([Ch. 18](18-advanced-directives.md))                              |

## Tail strategies

These get passed to a split (or the factor forms of `vectorize`/`unroll`) to say
how a split handles an extent that isn't a multiple of the factor:

| Strategy                     | Behavior                                                                                                                       |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `TailStrategy::RoundUp`      | Fastest; extends the loop and does extra safe work. Needs the producer valid past its extent (say, with a boundary condition). |
| `TailStrategy::ShiftInwards` | The default; shifts the last tile to overlap the previous one. Safe for pure Funcs.                                            |
| `TailStrategy::GuardWithIf`  | Adds an if-check; slowest, but always safe.                                                                                    |

## Code size and diagnostics

| Directive                                            | Effect                                                                                                                                         |
| ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `f.never_partition(var)` / `f.never_partition_all()` | Disable loop partitioning (prologue/steady-state/epilogue). A code-size and clarity knob, not a parallelism one.                               |
| `Target::Profile` (`HL_TARGET=host-profile`)         | Emit the per-Func profiler table. The primary diagnostic. ([Ch. 8](08-benchmarking-and-profiling.md))                                          |
| `f.print_loop_nest()`                                | Print the loop structure; check placement and vectorization shape. ([Ch. 12](12-reading-a-loop-nest.md), [Ch. 9](09-reading-the-stmt-file.md)) |
