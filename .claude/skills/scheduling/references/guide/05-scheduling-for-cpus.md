# 5. Scheduling for CPUs

Almost every fast CPU pipeline ends up with the same shape. One outer parallel
loop, vectorized along the stride-1 axis, with every non-trivial intermediate
computed inside that parallel loop. Most pipelines start here, and most of them
stay here. It's worth getting comfortable with this shape first, since the rest
of Part II is really just exceptions to it.

```cpp
const int vec = natural_vector_size<float>();
Var yo, yi;
output.split(y, yo, yi, /*strip*/ 32)
      .reorder(x, yi, c, yo)       // vectorized axis innermost; c inside for 3-chan
      .vectorize(x, vec)
      .parallel(yo);

// Every non-trivial intermediate computes inside the output's parallel loop:
producer_1.compute_at(output, yo).vectorize(x, vec);
producer_2.compute_at(output, yo).vectorize(x, vec);
```

## The three ingredients

1. **Vectorize the stride-1 axis** (usually `x`). The inner loop becomes SIMD.
2. **Parallelize an outer axis** with enough iterations.
   `parallel_tasks ≥ cores` is the floor. 1x to 4x cores is fine for evenly
   balanced work. If the natural outer dimension is too short to give even 1x
   cores, tile and `fuse(yo, xo, t).parallel(t)`.
3. **Every non-trivial intermediate goes `compute_at` inside the output's
   parallel loop.** This gives one fused parallel region. Each thread streams
   its strip of intermediates end-to-end through cache.

## Why this shape

A `compute_root` per stage, each with its own `parallel`, becomes one
thread-pool phase per stage. That's a barrier between each one, with
intermediates spilling to DRAM. Collapse everything into one fused parallel
region and the whole pipeline stays in cache.

**How to check it:** the profiler's pipeline-level `parallel loops` count should
be 1 (see [Benchmarking & Profiling](08-benchmarking-and-profiling.md)).
Per-Func, intermediates should show `parallel loops: 0`, folded into the
consumer.

## Task-count tuning

Two numbers from the profiler drive this: `parallel tasks` (per parallel loop)
and `active threads` (per Func). It's worth reading both before retuning.

- **Too few tasks.** `parallel tasks < cores` and `active threads` low. Try
  dropping the split factor, or fusing another axis (channels, an outer tile
  dim) into the parallel var. For example,
  `output.split(x, xo, xi, 64).parallel(xo)` gives 24 tasks on 64 cores. Fusing
  `c` with `fuse(c, xo, t).parallel(t)` brings that up to 72.
- **Too many tasks.** `parallel tasks` way over cores, and per-task
  `heap allocs` showing in the profile. Here, blocking iterations with
  `parallel(var, task_size)` helps. Given 2560 tasks spending real time in
  malloc, `parallel(y, 16)` blocks 16 rows per task, which drops that to 160.
- **Tasks around cores to 4x cores, and `active threads` near cores.** That's
  already a good zone, so there's no need to retune just for the sake of it.

For small fixed-extent dimensions like channels, `bound(c, 0, 3).unroll(c)`
usually works better than vectorizing or parallelizing `c`.

A couple of rules this shape leans on get their own chapters, since they're easy
to trip over. The parallel loop needs to be **outermost**, and it's worth
keeping an eye on how many recompute-multiplying axes sit outside a
`compute_at`. Both are covered in [Pitfalls](11-pitfalls.md).
