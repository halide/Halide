# Scheduling Halide Pipelines

This guide provides an overview of how Halide pipelines are scheduled, as well
as the strategies, directives and techniques that can be used to optimize
performance for a specific target.

The material is in four parts:

- **Part I, The Model.** The objects being scheduled, how a pipeline is
  realized, and how Halide infers the bounds of every buffer and loop.
- **Part II, Scheduling for Performance.** The shapes fast pipelines take on
  CPUs and GPUs, how to find what to fix, and the recipes and traps that come up
  in real work.
- **Part III, Scheduling Directives.** How to read a loop nest, then each
  directive and exactly what it does to it.
- **Part IV, Reference.** A directive index, a pre-flight checklist, and a
  worked example.

```{toctree}
:hidden:
:maxdepth: 1

scheduling/01-introduction
scheduling/02-the-programming-model
scheduling/03-realizing-a-pipeline
scheduling/04-bounds-inference
scheduling/05-scheduling-for-cpus
scheduling/06-scheduling-for-gpus
scheduling/07-what-to-schedule
scheduling/08-benchmarking-and-profiling
scheduling/09-reading-the-stmt-file
scheduling/10-recipes
scheduling/11-pitfalls
scheduling/12-reading-a-loop-nest
scheduling/13-defaults-and-inlining
scheduling/14-placement-compute-root-and-compute-at
scheduling/15-storage-levels
scheduling/16-reshaping-loops
scheduling/17-loop-types
scheduling/18-advanced-directives
scheduling/19-how-the-loop-nest-is-built
scheduling/20-directive-reference
scheduling/21-checklist-and-worked-example
```

## Table of contents

**Part I, The Model**

1. [Introduction](scheduling/01-introduction.md): algorithm vs schedule, and
   what the schedule controls.
2. [The Programming Model](scheduling/02-the-programming-model.md): Funcs,
   stages, RDoms, and the pipeline graph.
3. [Realizing a Pipeline](scheduling/03-realizing-a-pipeline.md): when
   computation actually runs; JIT vs AOT.
4. [Bounds Inference](scheduling/04-bounds-inference.md): how Halide sizes
   buffers and loops from the output request.

**Part II, Scheduling for Performance**

05. [Scheduling for CPUs](scheduling/05-scheduling-for-cpus.md): one parallel
    loop, vectorized inner, fused intermediates.
06. [Scheduling for GPUs](scheduling/06-scheduling-for-gpus.md): mapping loops
    onto GPU blocks and threads.
07. [What to Schedule](scheduling/07-what-to-schedule.md): inline vs
    `compute_at` vs `compute_root`.
08. [Benchmarking & Profiling](scheduling/08-benchmarking-and-profiling.md):
    measuring, and reading the profile.
09. [Reading the `.stmt` File](scheduling/09-reading-the-stmt-file.md): checking
    the vectorization shape.
10. [Recipes](scheduling/10-recipes.md): sliding windows, tiling, pyramids,
    stencil chains, histograms.
11. [Pitfalls](scheduling/11-pitfalls.md): recurrences, parallel placement,
    recompute multipliers, common mistakes.

**Part III, Scheduling Directives**

12. [Reading a Loop Nest](scheduling/12-reading-a-loop-nest.md): the
    `print_loop_nest` notation, and one Func's loops.
13. [Defaults and Inlining](scheduling/13-defaults-and-inlining.md): the inline
    default and the three compute levels.
14. [Placement: `compute_root` and `compute_at`](scheduling/14-placement-compute-root-and-compute-at.md):
    where a Func gets built.
15. [Storage Levels](scheduling/15-storage-levels.md): `store_at`, `store_root`,
    `hoist_storage`, sliding windows.
16. [Reshaping Loops](scheduling/16-reshaping-loops.md): `split`, `fuse`,
    `reorder`, `tile`.
17. [Loop Types](scheduling/17-loop-types.md): serial, parallel, vectorized,
    unrolled, GPU.
18. [Advanced Directives](scheduling/18-advanced-directives.md): `rfactor`,
    `in`/`clone_in`, `compute_with`, `specialize`.
19. [How the Loop Nest Is Built](scheduling/19-how-the-loop-nest-is-built.md):
    the full assembly steps.

**Part IV, Reference**

20. [Directive Reference](scheduling/20-directive-reference.md): every
    directive, its signature and effect.
21. [Checklist and Worked Example](scheduling/21-checklist-and-worked-example.md):
    a pre-flight checklist and an end-to-end example.

## Conventions

- Loop nests in Part III are what `Func::print_loop_nest()` prints.
  [Reading a Loop Nest](scheduling/12-reading-a-loop-nest.md) explains the
  notation.
- Code examples are shown inline as the body of a small `int main()` that builds
  the pipeline and calls `print_loop_nest()`.
- Part III skips two things `print_loop_nest` prints: exact loop-variable names
  and constant loop bounds. They come from bounds inference and never change the
  structure. More on that in
  [Bounds Inference](scheduling/04-bounds-inference.md).
