# 21. Checklist and Worked Example

## Pre-flight checklist

When a schedule seems done, it's worth a quick pass before moving on.

**Profile-driven.** For what to fix first when the profile shows a problem,
follow the priority list in
[Benchmarking and the Profiler](08-benchmarking-and-the-profiler.md).

**Schedule shape** (read the generator):

1. Every cheap pointwise Func left inlined?
2. Output parallelized (outer) and vectorized (inner, stride-1)?
3. Intermediates `compute_at` inside the output's outer parallel loop?
4. `bound` plus `unroll` on small fixed extents (channels)?
5. For histograms and data-dependent scatters, the update left unvectorized,
   with pure and update at the same site?

**`.stmt`** (worth checking when the profile looks clean but the timing is still
off):

6. Hot-loop stores are unit-stride ramps at `natural_vector_size<T>()` width?
7. No avoidable strided gathers or scatters?

## Worked example

Here's a tiny pipeline, `input → blur_y → blur_x → output`, taken from an
algorithm-only starting point to a good schedule.

### Iteration 1: `compute_root` everything

```cpp
blur_y.compute_root();
blur_x.compute_root();
```

Compile and benchmark. It works, and it's slow, and the profile shows it's all
serial. This is the working baseline rather than a good schedule.

### Iteration 2: parallelize and vectorize

```cpp
const int vec = natural_vector_size<float>();
Var yo, yi;
output.split(y, yo, yi, 32).parallel(yo).vectorize(x, vec);
blur_y.compute_root().parallel(y, 32).vectorize(x, vec);
blur_x.compute_root().parallel(y, 32).vectorize(x, vec);
```

Much faster. Here's the profile (mock):

```
total: 1.20 ms.  avg_threads: 28.5  parallel loops: 3  parallel tasks: 240
peak heap usage: 24 MB

  blur_y | 0.72 ms (60.0%) | 30.0 | 1 par_loop | 80 tasks | 1 alloc, 16 MB | recompute=1.00
  blur_x | 0.18 ms (15.0%) | 30.0 | 1 par_loop | 80 tasks | 1 alloc,  8 MB | recompute=1.00
  output | 0.12 ms (10.0%) | 30.0 | 1 par_loop | 80 tasks
```

Reading it by priority: pipeline `parallel loops` = 3, so there are three
parallel regions with a barrier between each, and both `compute_root` buffers
(24 MB) get fully built before they're read once. There are no recompute
problems here. The issue is the pipeline shape, not parallelism.

### Iteration 3: collapse to one region with a sliding window

```cpp
output.split(y, yo, yi, 32).parallel(yo).vectorize(x, vec);
blur_x.store_at(output, yo).compute_at(output, yi).vectorize(x, vec);
blur_y.store_at(output, yo).compute_at(output, yi).vectorize(x, vec);
```

Faster still. Profile (mock):

```
total: 0.42 ms.  avg_threads: 30.5  parallel loops: 1  parallel tasks: 80
peak heap usage: 240 KB

  output  | 0.38 ms (90.5%) | 31.0 | 1 par_loop | 80 tasks
  ├blur_x | (folded into output) | 80 small allocs, 240 KB | recompute=1.00
  └blur_y | (folded into output) | 80 small allocs, 120 KB | recompute=1.00
```

Pipeline `parallel loops` = 1, peak heap dropped 100x, and recompute stayed at
1.00. This is a good place to stop, or to tune the strip height once more.

## When in doubt

- Start with every non-trivial Func at `compute_root()`, plus sensible
  parallel/vectorize at the output. Then pull producers inward with
  `compute_at`.
- If a change is more than a few lines of scheduling code, test after each line.
- Lean on the profiler and the `.stmt` file rather than guessing.
