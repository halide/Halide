# 4. Bounds Inference

Halide works out memory sizes and loop bounds automatically. There's no need to
declare how big an intermediate buffer is, or how many iterations a loop runs. A
mechanism called **bounds inference** figures all of that out, driven entirely
by the region requested from the final output.

This is also the piece the rest of the manual sets aside:
[Reading a Loop Nest](12-reading-a-loop-nest.md) ignores constant loop bounds
precisely because they come from here, not from the schedule's structure.

## The demand-driven model

Bounds inference runs top-down, in reverse. Realizing a pipeline requests a
rectangular region of the output `Func`
([Realizing a Pipeline](03-realizing-a-pipeline.md)). Halide then walks the
dependency graph backward, computing the region (the "bounds") of every
intermediate `Func`, input `Image`, and `Buffer` needed to satisfy that output
request.

- **Outputs drive inputs.** A request for a 100x100 output tells Halide exactly
  which input pixels are needed to produce those 10,000 values.
- **Automatic padding.** When the algorithm has a stencil (a blur, say), bounds
  inference grows the required input box to cover the stencil's footprint.

## How bounds propagate

Take a simple 1D three-tap blur:

```cpp
Func input, blur;
Var x;

blur(x) = (input(x - 1) + input(x) + input(x + 1)) / 3.0f;
```

Requesting `blur` over `x = [0, 99]` (100 elements), bounds inference reads the
index expressions:

- Computing `blur(0)` needs `input` down to `0 - 1 = -1`.
- Computing `blur(99)` needs `input` up to `99 + 1 = 100`.

So `input` has to be valid over `[-1, 100]`. If `input` is an external buffer,
Halide asserts that the provided buffer covers that range. If `input` is an
intermediate `Func`, Halide computes it over `[-1, 100]` before computing
`blur`.

## Managing and debugging bounds

Because bounds inference works from the arithmetic relationships between
coordinates, an overly complex or data-dependent access pattern can stop Halide
from finding a bounded region.

- **Boundary conditions.** Near the edges of an input, bounds inference may ask
  for pixels outside the allocated image (like index `-1` above). A
  `BoundaryConditions` wrapper (`repeat_edge`, `mirror_image`, and so on) clamps
  or mirrors the coordinates, keeping the access in-bounds while still
  satisfying the inference engine. This is why the performance chapters keep
  recommending one on inputs read with offsets.
- **Trace and inspect.** When a pipeline fails on an unbounded region or an
  alarming allocation, `Func::trace_stores()` or compiling with
  `HL_DEBUG_CODEGEN=1` shows the bounds Halide is trying to infer as it runs.

A related failure is worth recalling from
[The Programming Model](02-the-programming-model.md): forcing an index helper to
be non-inline hides its arithmetic from this analysis, which can turn a
well-bounded pipeline into an unbounded one.
