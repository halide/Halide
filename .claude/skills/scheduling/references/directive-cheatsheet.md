# Halide scheduling directive cheat sheet

A compact, one-screen lookup. Each directive is a method on `Func` (or, for an
update stage, `Func::update(int)`). For the full prose reference and examples,
see `guide/20-directive-reference.md`; the group headings below note the chapter
that explains the mechanics.

## Where to compute and store — ch. 13–15

| Directive                       | Effect                                                                           |
| ------------------------------- | -------------------------------------------------------------------------------- |
| `f.compute_root()`              | Compute all of `f` once, before any consumer.                                    |
| `f.compute_at(g, var)`          | Compute the needed slice of `f` inside `g`'s `var` loop.                         |
| `f.compute_inline()`            | Reset to the inline default (undo a `compute_*`).                                |
| `f.store_at(g, var)`            | Allocate `f` at `g`'s `var` loop; coarser than compute enables a sliding window. |
| `f.store_root()`                | Allocate `f` at the outermost level.                                             |
| `f.hoist_storage(g, var)`       | Hoist only the allocation to `g`'s `var` loop. Never past a parallel loop.       |
| `f.fold_storage(var, K)`        | Circular buffer of `K` slots in `var` (sliding windows).                         |
| `f.store_in(MemoryType::Stack)` | Allocate on the stack; small fixed sizes only.                                   |

## Loop shape — ch. 16

| Directive                              | Effect                                                                     |
| -------------------------------------- | -------------------------------------------------------------------------- |
| `f.split(x, xo, xi, factor)`           | Split `x` into outer `xo`, inner `xi` (`x = xo*factor + xi`).              |
| `f.fuse(a, b, t)`                      | Collapse two adjacent loops into one `t`.                                  |
| `f.tile(x, y, xo, yo, xi, yi, tx, ty)` | Two splits plus a reorder into tiled traversal.                            |
| `f.reorder(a, b, c, …)`                | Reorder loops, innermost first (last arg = outermost).                     |
| `f.reorder_storage(a, b, …)`           | Change memory layout, innermost dim first.                                 |
| `f.bound(x, min, extent)`              | Promise `x`'s range at compile time (enables fixed-size unroll/vectorize). |

## Loop types / execution — ch. 17

| Directive                                        | Effect                                                                        |
| ------------------------------------------------ | ----------------------------------------------------------------------------- |
| `f.parallel(var[, task_size])`                   | Run `var` on the thread pool; `task_size` blocks it into fewer, larger tasks. |
| `f.vectorize(var[, factor])`                     | Emit SIMD; prefer `factor = natural_vector_size<T>()`.                        |
| `f.unroll(var[, factor])`                        | Unroll the loop (stays scalar).                                               |
| `f.serial(var)`                                  | Reset a dimension to serial.                                                  |
| `f.gpu_tile(…)`, `f.gpu_blocks/threads/lanes(…)` | Map dimensions to GPU blocks/threads/lanes.                                   |

## Wrappers, reductions, variants — ch. 18

| Directive                                       | Effect                                                         |
| ----------------------------------------------- | -------------------------------------------------------------- |
| `f.in(g)`                                       | Identity wrapper of `f` for consumer(s) `g`.                   |
| `f.clone_in(g)`                                 | Fresh recomputed copy of `f` for `g`, scheduled independently. |
| `f.update(n).rfactor(rv, v)`                    | Factor a reduction so the preserved axis parallelizes.         |
| `b.compute_with(a, var)`                        | Fuse two stages into one shared loop nest down to `var`.       |
| `f.specialize(cond)` / `f.specialize_fail(msg)` | Run-time-selected schedule variant / hard fallback.            |

## Tail strategies — passed to a split (or the factor forms of vectorize/unroll)

| Strategy                     | Behavior                                                                           |
| ---------------------------- | ---------------------------------------------------------------------------------- |
| `TailStrategy::RoundUp`      | Fastest; overshoots and does extra work. Needs the producer valid past its extent. |
| `TailStrategy::ShiftInwards` | Default; overlaps the last tile with the previous one. Safe for pure Funcs.        |
| `TailStrategy::GuardWithIf`  | Adds an if-check; slowest, always safe.                                            |

## Diagnostics

| Tool                                         | Use                                                                         |
| -------------------------------------------- | --------------------------------------------------------------------------- |
| `f.print_loop_nest()`                        | Print the loop structure; check placement and vectorization shape (ch. 12). |
| `HL_TARGET=host-profile` / `Target::Profile` | Per-Func profiler table — the primary diagnostic (ch. 8).                   |
| `HL_DEBUG_CODEGEN=1`                         | Dump IR after key lowering passes (raise the number for more detail).       |

## Gotchas

- **`For` loops carry an inclusive `min`/`max`**, not `min`/`extent` — a common
  source of apparent off-by-one errors.
- **`reorder` lists innermost first**; the last argument is the outermost loop.
- **`store_at` and `compute_at` are independent** — storing coarser than you
  compute is exactly what enables sliding-window reuse.
- **Don't `store_at`/`hoist_storage` past a `parallel` loop** that would race on
  the shared allocation.
- **A serial recurrence can't be parallelized or vectorized** along the
  dependent axis — see ch. 11.
- **`RoundUp` needs the producer valid past its extent** (e.g. via a boundary
  condition); otherwise keep the default `ShiftInwards`.
- **Vectorize width should be `natural_vector_size<T>()`**, not a guessed
  constant.
