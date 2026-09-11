# 3. Realizing a Pipeline

Defining an algorithm and a schedule only builds an in-memory description of the
computation. No pixels are processed during setup. The work happens when the
pipeline is **realized**.

## The realization trigger

Realization is running the pipeline to produce actual data. It starts either by
calling `.realize()` on the output `Func`, or by invoking a pre-compiled
function the Halide compiler generated earlier.

Realizing takes an explicit output size, such as a 1920x1080 image. That request
drives everything downstream: it kicks off **bounds inference**
([Bounds Inference](04-bounds-inference.md)), which works out how much of each
input and intermediate is needed to produce that exact output footprint.

## JIT vs. AOT

There are two ways to realize a pipeline, depending on the deployment.

- **Just-In-Time (JIT).** Good for prototyping and testing.
  `Func::realize(width, height)` compiles the pipeline to machine code on the
  spot and runs it in the same process, returning the result in a Halide
  `Buffer`.
- **Ahead-Of-Time (AOT).** Good for production. Instead of calling `realize()`,
  the pipeline is compiled to an object file plus a C header. The application
  links those artifacts and calls the generated C function, passing in
  pre-allocated input and output buffers. A pipeline that only ever runs
  precompiled AOT code can link against the standalone `halide-runtime` package,
  dropping the compiler and LLVM dependencies entirely.

## The realization lifecycle

JIT or AOT, the realization sequence follows the same path, built to keep memory
safe and performance high:

1. **Bounds inference.** The requested output region is propagated backward
   through the algorithm to work out the required region of every input and
   intermediate `Func` (see [Bounds Inference](04-bounds-inference.md)).
2. **Memory allocation.** Temporary buffers are allocated for the intermediates
   the schedule materializes, such as anything at `compute_root`
   ([Placement](14-placement-compute-root-and-compute-at.md)).
3. **Thread-pool activation.** If the schedule marks any loops `parallel`,
   Halide hands the work to its internal thread pool.
4. **Compute and store.** The expressions are evaluated in the order the
   schedule dictates, filling the output buffer.
5. **Deallocation.** Temporary memory is freed before control returns to the
   caller.

That first step is where most of the interesting work happens, and it's the
subject of the next chapter.
