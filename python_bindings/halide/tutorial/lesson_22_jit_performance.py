#!/usr/bin/python3

# Halide tutorial lesson 22: JIT compilation performance

# This lesson demonstrates the various performance implications of the
# various Halide methods of doing "Just-In-Time" compilation.

import time

import halide as hl


def benchmark(samples, iterations, op):
    """A minimal stand-in for Halide::Tools::benchmark(): runs `op`
    `iterations` times per sample, and returns the minimum time (in
    seconds) for one iteration, over `samples` samples."""
    best = None
    for _ in range(samples):
        start = time.perf_counter()
        for _ in range(iterations):
            op()
        elapsed = (time.perf_counter() - start) / iterations
        if best is None or elapsed < best:
            best = elapsed
    return best


def make_pipeline():
    # We'll start with a simple transpose operation...
    input, output = hl.Func("input"), hl.Func("output")
    x, y = hl.Var("x"), hl.Var("y")

    # Fill the input with a linear combination of the coordinate values...
    input[x, y] = hl.u16(x + y)
    input.compute_root()

    # Transpose the rows and cols
    output[x, y] = input[y, x]

    # Schedule it ... there's a number of possibilities here to do an
    # efficient block-wise transpose.
    xi, yi = hl.Var("xi"), hl.Var("yi")

    # Let's focus on 8x8 subtiles, and then vectorize across X, and unroll
    # across Y.
    output.tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi)

    # For more advanced scheduling:
    #
    # We can improve this even more by using the .in_() directive (see
    # Tutorial 19), which allows us to interpose new Funcs in between input
    # and output.
    #
    # Here we can inject a block_transpose function to allow us to do 8
    # vectorized loads from the input.
    block_transpose = input.in_(output).compute_at(output, x).vectorize(x).unroll(y)
    #
    # And now let's reorder and vectorize in X across the block.
    _block = (
        block_transpose.in_(output)
        .reorder_storage(y, x)
        .compute_at(output, x)
        .vectorize(x)
        .unroll(y)
    )

    # Return the constructed pipeline
    return hl.Pipeline(output)


def main():
    # Since we'll be using the same sample and iteration counts for our
    # benchmarking, let's define them here in the outermost scope.
    samples = 100
    iterations = 1

    # Now, let's measure the performance of constructing and executing a
    # simple pipeline from scratch...
    if True:
        count = 0

        def op():
            nonlocal count
            # First, create an output buffer to hold the results.
            result = hl.Buffer(hl.UInt(16), [1024, 1024])

            # Now, construct our pipeline from scratch.
            pipeline = make_pipeline()

            # And then call realize to execute the pipeline.
            pipeline.realize(result)
            count += 1

        t = benchmark(samples, iterations, op)

        # On a MacBook Pro M1, we should get around ~1800 times/sec.
        print(f"Compile & Execute Pipeline (from scratch): {int(count / t)} times/sec")

    # This time, let's create the pipeline outside the timing loop and
    # re-use it for each execution...
    if True:
        # Create our pipeline, and re-use it in the loop below
        pipeline = make_pipeline()

        count = 0

        def op():
            nonlocal count
            # Create our output buffer
            result = hl.Buffer(hl.UInt(16), [1024, 1024])

            # Now, call realize
            pipeline.realize(result)
            count += 1

        t = benchmark(samples, iterations, op)

        # On a MacBook Pro M1, we should get around ~175000 times/sec (almost
        # 95-100x times faster!).
        print(
            f"Compile & Execute Pipeline (re-use pipeline): {int(count / t)} times/sec"
        )

    # Let's do the same thing as before, but explicitly JIT compile before we
    # realize...
    if True:
        pipeline = make_pipeline()

        # Let's JIT compile for our target before we realize, and see what
        # happens...
        target = hl.get_jit_target_from_environment()
        pipeline.compile_jit(target)

        count = 0

        def op():
            nonlocal count
            result = hl.Buffer(hl.UInt(16), [1024, 1024])
            pipeline.realize(result)
            count += 1

        t = benchmark(samples, iterations, op)

        # On a MacBook Pro M1, this should be about the same as the previous
        # run (about ~175000 times/sec)
        #
        # This may seem somewhat surprising, since compiling before realizing
        # doesn't seem to make much of a difference to the previous case.
        # However, the first call to realize() will implicitly JIT-compile
        # and cache the generated code associated with the Pipeline object,
        # which is basically what we've done here. Each subsequent call to
        # realize uses the cached version of the native code, so there's no
        # additional overhead, and the cost is amortized as we re-use the
        # pipeline.
        print(f"Execute Pipeline (compile before realize): {int(count / t)} times/sec")

        # Another subtlety is the creation of the result buffer ... the
        # declaration implicitly allocates memory which will add overhead to
        # each loop iteration. This time, let's try using the
        # realize([1024, 1024]) call which will use the buffer managed by
        # the pipeline object for the outputs...
        count = 0

        def op():
            nonlocal count
            pipeline.realize([1024, 1024])
            count += 1

        t = benchmark(samples, iterations, op)

        # On a MacBook Pro M1, this should be about the same as the previous
        # run (about ~175000 times/sec).
        print(
            f"Execute Pipeline (same but with realize([])): {int(count / t)} times/sec"
        )

        # Or ... we could move the declaration of the result buffer outside
        # the timing loop, and re-use the allocation (with the caveat that
        # we will be stomping over its contents on each execution).
        result = hl.Buffer(hl.UInt(16), [1024, 1024])

        count = 0

        def op():
            nonlocal count
            pipeline.realize(result)
            count += 1

        t = benchmark(samples, iterations, op)

        # On a MacBook Pro M1, this should be much more efficient ...
        # ~200000 times/sec (or 10-12% faster).
        print(
            f"Execute Pipeline (re-use buffer with realize): {int(count / t)} times/sec"
        )

    # Alternatively, we could compile to a Callable object...
    if True:
        pipeline = make_pipeline()
        target = hl.get_jit_target_from_environment()

        # Here, we can ask the pipeline for its argument list (these are
        # either Params, ImageParams, or Buffers) so that we can construct a
        # Callable object with the same calling convention.
        arguments = pipeline.infer_arguments()

        # The Callable object acts as a convenient way of invoking the
        # compiled code like a function call, using an argv-like syntax for
        # the argument list. It also caches the JIT compiled code, so
        # there's no code generation overhead when invoking the callable
        # object and executing the pipeline.
        callable = pipeline.compile_to_callable(arguments, target)

        # Again, we'll pre-allocate and re-use the result buffer.
        result = hl.Buffer(hl.UInt(16), [1024, 1024])

        count = 0

        def op():
            nonlocal count
            callable(result)
            count += 1

        t = benchmark(samples, iterations, op)

        # This should be about the same as the previous run (about ~200000
        # times/sec).
        print(f"Execute Pipeline (compile to callable): {int(count / t)} times/sec")

    # Let's see how much time is spent on just compiling...
    if True:
        pipeline = make_pipeline()

        # Only the first call to compile_jit() is expensive ... after the
        # code is generated, it gets stored in a cache for later re-use, so
        # repeatedly calling compile_jit has very little overhead after its
        # been cached.

        count = 0

        def op():
            nonlocal count
            pipeline.compile_jit()
            count += 1

        t = benchmark(samples, iterations, op)

        # Only the first call does any work and the rest are essentially
        # free. On a MacBook Pro M1, we should expect ~2 billion times/sec.
        print(f"Compile JIT (using cache): {int(count / t)} times/sec")

        # You can invalidate the cache manually, which will destroy all the
        # compiled state.
        count = 0

        def op():
            nonlocal count
            pipeline.invalidate_cache()
            pipeline.compile_jit()
            count += 1

        t = benchmark(samples, iterations, op)

        # This is an intentionally expensive loop, and very slow!
        # On a MacBook Pro M1, we should see only ~2000 times/sec.
        print(f"Compile JIT (from scratch): {int(count / t)} times/sec")

    # Alternatively we could compile to a Module...
    if True:
        pipeline = make_pipeline()
        args = pipeline.infer_arguments()

        # Compiling to a module generates a self-contained Module containing
        # an internal-representation of the lowered code suitable for
        # further compilation. So, it's not directly runnable, but it can be
        # used to link/combine Modules and generate object files, static
        # libs, bitcode, etc.

        count = 0

        def op():
            nonlocal count
            pipeline.compile_to_module(args, "transpose")
            count += 1

        t = benchmark(samples, iterations, op)

        # On a MacBook Pro M1, this should be around ~10000 times/sec
        print(f"Compile to Module: {int(count / t)} times/sec")

    print("DONE!")
    return 0


if __name__ == "__main__":
    main()
