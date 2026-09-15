# 8. Benchmarking & Profiling

A good approach for optimizing a pipeline's performance is to work in a loop:
measure, read the profile, fix the worst thing, and measure again. The loop is
only as good as the measurement, so most of the discipline goes into not being
misled by noise.

## Benchmarking best practices

By default, the Halide runtime will use every core available. Any other
resource-heavy process that may be running can distort a measured runtime.
Before trusting a number, it's worth a quick sanity check.

- Make sure nothing else expensive is running: no other benchmark, no background
  build, no second profiler.
- If a number looks surprising — say, 5x to 10x worse than expected — resist the
  urge to conclude the change regressed. Check for stray processes first, then
  re-run. Unstable numbers are almost always noise rather than signal.

Timings with lots of variance between runs can be misleading, and might send you
down the wrong path.

It's also worth timing the pipeline single-threaded — set `HL_NUM_THREADS=1`, or
call `halide_set_num_threads(1)` from code. Comparing that serial baseline
against the full multi-core run shows how well the schedule scales: if adding
cores doesn't buy a roughly proportional speedup, the pipeline has a scaling
problem — too few parallel tasks, a parallel loop that isn't outermost, or a
memory-bandwidth ceiling — and the profiler's `average threads used` usually
points the same way.

## The profiler is the main tool

Profiling is turned on by adding `Target::Profile` to the target (for example,
`HL_TARGET=host-profile`). The runtime then prints a per-Func table to `stdout`,
along with explicit warnings for known antipatterns, and it's worth reading on
every iteration.

The same numbers can be captured for offline use. Setting
`HL_PROFILER_JSON_OUTPUT=<filename>` makes the runtime also write the raw
per-Func stats to that file as JSON, which is handy for comparing runs over time
or feeding the numbers to a script rather than scraping the printed table.

Here's a sample, from a max-filter pipeline:

```
total time: 410 ms  runs: 9  time per run: 45.6 ms
average threads used: 27.5  parallel loops: 5  parallel tasks: 2262
heap allocations: 17289   peak heap usage: 384 MB

 funcs ........... | active     | active  | parallel  | heap | peak | avg  |recompute| notes
                   |            | threads | loops|tasks|allocs|  mem |  mem |  ratio  |
  thread idle      |  9.08 19.9%|  24.9   |      |     |      |      |      |         |
  free             | 16.62 36.4%|   1.0   |      |     |      |      |      |         |
  vert_log         |  6.32 13.8%|  42.0   |   4  | 342 |   1  | 304M | 304M |  1.00   |  1
  output           | 11.84 25.9%|  62.5   |   1  |1920 |      |      |      |  1.00   |
  ├vert            |  0.35  0.7%|  62.3   |      |     |1920  |  80M | 1.2M |  1.75   |
  └maximum         |  1.39  3.0%|  61.8   |      |     |      |  64  |      |  1.00   |

Performance warnings:
 1) vert_log launches 4 parallel loops and shows poor utilization of the
    thread pool. Ensure the parallel loop is the outermost one. ...
```

The habit worth building is to fix the worst column of the hottest Func first.

### Top-line stats

- **`time per run`.** The number to minimize.
- **`average threads used`.** Much lower than the core count usually means the
  pipeline is under-parallelized. Look for the Func with high `active` time and
  low `active threads`.
- **`parallel loops`** (pipeline-level). The total parallel-for calls per run.
  Aim for just 1 when following the standard shape. More than 1 means either the
  parallel loop isn't outermost (see [Pitfalls](11-pitfalls.md)), or there's
  more than one parallel region. That second case is sometimes deliberate (see
  [What to Schedule](07-what-to-schedule.md)), but it's worth collapsing when it
  isn't.
- **`peak heap usage`.** When this is a large fraction of L3 (or larger than
  DRAM bandwidth times runtime), some `compute_root` is probably too eager.

### Per-Func columns

- **`active` / `active threads`.** Where time goes, and how parallel it was. Low
  active threads on a meaningful Func points to under-parallelization.
- **`parallel loops`.** Ideally 1, or 0 when it's folded into a parent's
  parallel loop. Over 1 usually means several update stages with separate
  `.parallel()` calls, or the pure and update stages each carrying their own.
- **`parallel tasks`.** Aim for at least as many as there are cores (1x is the
  floor), with 1x to 4x a comfortable range. Tiny counts tend toward starvation,
  huge counts toward dispatch overhead. See
  [Scheduling for CPUs](05-scheduling-for-cpus.md).
- **`heap allocs`.** When this scales with task count and the Func is small,
  `hoist_storage` or `store_in(MemoryType::Stack)` can help.
- **`peak mem` / `avg mem`.** A large peak with one allocation points to a
  `compute_root`. A large peak with many points to many concurrent per-task
  slabs.
- **`recompute ratio`.** Cells produced divided by cells actually needed, where
  1.0 is ideal. Anything above 1.0 is redundant work, and it's almost always a
  `compute_at` inside a tiled consumer whose per-tile footprint keeps growing.
  1.5x to 2x is tolerable; 5x and up is a red flag, and usually means changing
  the `compute_at` level or the tile axis (see [Pitfalls](11-pitfalls.md)).

## What to fix first

Roughly in priority order:

1. **Pipeline `parallel loops` > 1.** Collapse the regions; the profiler names
   the Func.
2. **Any Func with `recompute ratio` well over 1.** A wrong `compute_at` level,
   or the wrong tile axis.
3. **Top-line `peak heap` well over L3.** Shrink the largest `compute_root` with
   `compute_at` or `clone_in`.
4. **`free` showing up as a top-level cost with `active threads = 1`.** That's
   the single-threaded teardown of a huge `compute_root` buffer, the same fix as
   #3.
5. **The hottest Func with low `active threads`.** Its parallel loop probably
   isn't outermost, or its task count is too small.

The profile alone drives most iterations, so it's the first thing to open. The
`.stmt` file (see [Reading the `.stmt` File](09-reading-the-stmt-file.md)) is
mainly for questions about vectorization shape.
