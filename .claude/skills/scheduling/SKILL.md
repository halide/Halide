---
name: scheduling
description: >-
  Guidance for scheduling Halide pipelines — deciding how a pipeline is
  computed, stored, vectorized, and parallelized to run fast on a target,
  without changing what it computes. Use this whenever the task involves
  writing, reviewing, or optimizing a Halide schedule, or reaching for any
  scheduling directive (compute_root, compute_at, store_at, store_root,
  hoist_storage, vectorize, parallel, unroll, split, tile, fuse, reorder,
  gpu_tile/gpu_blocks/gpu_threads, rfactor, compute_with, in/clone_in,
  specialize, bound, ...). Also use it for reading `print_loop_nest` output,
  diagnosing a slow or high-memory Halide pipeline, choosing inline vs
  compute_at vs compute_root, or interpreting bounds inference. Prefer this
  skill over guessing at directive semantics or schedule shape — it routes to
  an in-repo guide with a chapter per directive. Trigger it even when the user
  only says a Halide pipeline is "slow", mentions vectorization/parallelism/
  tiling/sliding windows, or asks how a `Func` actually gets computed, without
  saying "scheduling" explicitly.
---

# Scheduling Halide Pipelines

Halide splits every program into an **algorithm** (what each value is) and a
**schedule** (when and where each value is computed and stored). The schedule
can't change the result — it only moves three levers, and nearly all
performance comes from them:

- the **order** values are computed in,
- how much **redundant recomputation** happens, and
- how much **temporary storage** the pipeline needs.

This skill routes to a detailed in-repo guide rather than reproducing it. Read
the chapter a task actually needs *before* writing or changing a schedule: the
directives have precise, easy-to-misuse semantics (`store_at` and `compute_at`
control storage and compute *independently*; a misplaced `parallel` can
serialize, race, or explode memory; `For` loops carry an inclusive `min`/`max`,
not `min`/`extent`). Confirm the result with `print_loop_nest()` rather than
assuming.

## Start here

Read the landing page first — it has the full table of contents and the
four-part structure:

- **`doc/SchedulingHalidePipelines.md`**

For a fast, one-screen lookup — every directive's signature and effect, the tail
strategies, and the common gotchas — without opening a chapter, read the bundled
**`references/directive-cheatsheet.md`** (alongside this skill).

The chapters live under **`doc/scheduling/`**. Repo paths below are relative to
the repository root; the cheat-sheet path is relative to this skill.

## Which chapter to read

Jump to what the task needs instead of reading front to back.

**The model** (what is being scheduled)

- `doc/scheduling/01-introduction.md` — algorithm vs schedule; the three levers.
- `doc/scheduling/02-the-programming-model.md` — Funcs, stages, RDoms, the graph.
- `doc/scheduling/03-realizing-a-pipeline.md` — when computation runs; JIT vs AOT.
- `doc/scheduling/04-bounds-inference.md` — how buffers and loops get sized.

**Making it fast** (start here for "make this faster")

- `doc/scheduling/05-scheduling-for-cpus.md` — the canonical fast-CPU shape.
- `doc/scheduling/06-scheduling-for-gpus.md` — mapping loops onto blocks/threads.
- `doc/scheduling/07-what-to-schedule.md` — inline vs compute_at vs compute_root.
- `doc/scheduling/08-benchmarking-and-profiling.md` — measure; read the profile.
- `doc/scheduling/09-reading-the-stmt-file.md` — check the vectorization shape.
- `doc/scheduling/10-recipes.md` — sliding windows, tiling, pyramids, stencils, histograms.
- `doc/scheduling/11-pitfalls.md` — recurrences, parallel placement, recompute traps.

**Directive mechanics** (what a directive does to the loop nest)

- `doc/scheduling/12-reading-a-loop-nest.md` — the `print_loop_nest` notation.
- `doc/scheduling/13-defaults-and-inlining.md` — the inline default; three compute levels.
- `doc/scheduling/14-placement-compute-root-and-compute-at.md` — where a Func is built.
- `doc/scheduling/15-storage-levels.md` — `store_at`, `store_root`, `hoist_storage`.
- `doc/scheduling/16-reshaping-loops.md` — `split`, `fuse`, `reorder`, `tile`.
- `doc/scheduling/17-loop-types.md` — serial, parallel, vectorized, unrolled, GPU.
- `doc/scheduling/18-advanced-directives.md` — `rfactor`, `in`/`clone_in`, `compute_with`, `specialize`.
- `doc/scheduling/19-how-the-loop-nest-is-built.md` — the full assembly order.

**Reference**

- `doc/scheduling/20-directive-reference.md` — every directive, its signature and effect.
- `doc/scheduling/21-checklist-and-worked-example.md` — pre-flight checklist + end-to-end example.

## Inspecting a schedule

- `Func::print_loop_nest()` prints the loop nest a schedule produces — the
  fastest way to confirm it did what you meant. Chapter 12 explains the notation.
- `HL_DEBUG_CODEGEN=1` (raise for more detail) prints the IR after key lowering
  passes; `HL_JIT_TARGET` / `HL_TARGET` set the target being scheduled for.
- The built-in profiler and the `.stmt` file (chapters 8–9) show where time goes
  and whether the hot loop is a unit-stride vector store.

## Related material in this repo

- **Tutorials:** `tutorial/lesson_05_scheduling_1.cpp`,
  `tutorial/lesson_08_scheduling_2.cpp`, `tutorial/lesson_12_using_the_gpu.cpp`,
  `tutorial/lesson_18_parallel_associative_reductions.cpp`.
- **Scheduling API:** directive declarations live in `src/Func.h` and
  `src/Schedule.h`; the passes that consume a schedule are in
  `src/ScheduleFunctions.cpp`, `src/Bounds.cpp`, and `src/BoundsInference.cpp`.
- **Autoschedulers** (let Halide propose a schedule via
  `Pipeline::apply_autoscheduler`): `src/autoschedulers/` — `mullapudi2016`,
  `adams2019`, `li2018`, `anderson2021`.
- **Python bindings:** the guide's examples are C++, but the scheduling API is
  identical in Python — the same directives are methods on `halide.Func`, so a
  C++ schedule maps over directly. Python tutorials mirror the C++ ones under
  `python_bindings/halide/tutorial/`.
