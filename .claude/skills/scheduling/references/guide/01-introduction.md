# 1. Introduction

Every Halide program comes in two parts, written separately, and keeping them
apart is central to how the language works.

The first part is the **algorithm**. It says what each value is, and nothing
about how that value is computed. A program declares `Func`s and defines them as
pure functions of their argument `Var`s, with optional *update definitions* that
refine those values. At this stage the description is pure math, with nothing
said about execution.

The second part is the **schedule**. It says when and where each of those values
gets computed and stored. The schedule is written as directives (`compute_root`,
`compute_at`, `vectorize`, and so on) attached to each `Func`, and it's where
all of the performance decisions live.

What makes this split so useful is that the schedule can't change the answer.
For a given algorithm and input, the result is the same no matter how it's
scheduled, apart from small floating-point effects like round-off or overflow.
That leaves a lot of room to maneuver: the computation can be rearranged
aggressively in the name of speed, with no risk of quietly changing what the
program computes.

So if the schedule leaves the result alone, what does it actually affect? It
really comes down to three things:

- the **order** values get computed in,
- how much **redundant recomputation** happens, and
- how much **temporary storage** the pipeline needs.

Those three levers are where nearly all of the performance comes from. The gap
between a careless schedule and a careful one can be orders of magnitude, and a
poorly chosen schedule can even fail to compile, all while computing exactly the
same thing.

## What the schedule controls

For each `Func`, and for each of its stages, the schedule decides a few things:

- **Where it's computed.** Inlined into its consumer, computed at some loop
  level of a consumer, or computed once at the top (what Halide calls "root").
- **Where its storage lives.** This can sit at a coarser loop than where the
  values are computed, which is exactly what lets a sliding window reuse them.
- **How its loops are shaped.** They can be split, tiled, and reordered, and
  marked parallel, vectorized, or unrolled.
- **How its storage is laid out** in memory.

Later chapters take these one at a time. For now the takeaway is that scheduling
is a handful of independent choices, made per Func.

## What this manual covers

**Part I** lays the groundwork: the objects being scheduled, how a pipeline is
realized, and how Halide infers the bounds of every buffer and loop.

**Part II** is the **strategy**: given a pipeline, which schedule is worth
writing, and how to spot what to fix. It's the fast path to a schedule that runs
well.

**Part III** is the **mechanics**: given a schedule, what loop nest does it
actually produce? It opens by reading `Func::print_loop_nest()` output, then
works through each directive one at a time. When Part II reaches for a
directive, its precise effect lives here.

**Part IV** is a compact reference: a directive index, a checklist, and a worked
example.
