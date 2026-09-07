#!/usr/bin/python3

# Halide tutorial lesson 16: RGB images and memory layouts: running the
# compiled pipeline

# Before reading this file, see lesson_16_rgb_generate.py

# This is the code that actually uses the Halide pipeline we've
# compiled. It does not depend on libHalide, so we won't do
# "import halide".
#
# Instead, it depends on the Python extension modules that
# lesson_16_rgb_generate produced when we ran it with
# -e python_extension for each of the four layouts:
import time

import brighten_either
import brighten_interleaved
import brighten_planar
import brighten_specialized

import halide as hl


def check_timing(faster, slower):
    if faster > slower:
        print(
            f"Warning: performance was worse than expected. {faster} should be "
            f"less than {slower}"
        )


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


def main():
    # Let's make some images stored with interleaved and planar
    # memory. hl.Buffer is planar by default.
    planar_input = hl.Buffer(hl.UInt(8), [1024, 768, 3])
    planar_output = hl.Buffer(hl.UInt(8), [1024, 768, 3])
    interleaved_input = hl.Buffer.make_interleaved(hl.UInt(8), 1024, 768, 3)
    interleaved_output = hl.Buffer.make_interleaved(hl.UInt(8), 1024, 768, 3)

    # Let's check the strides are what we expect, given the
    # constraints we set up in the generator.
    assert planar_input.dim(0).stride() == 1
    assert planar_output.dim(0).stride() == 1
    assert interleaved_input.dim(0).stride() == 3
    assert interleaved_output.dim(0).stride() == 3
    assert interleaved_input.dim(2).stride() == 1
    assert interleaved_output.dim(2).stride() == 1

    # We'll now call the various functions we compiled and check the
    # performance of each.

    samples = 1
    iterations = 1000

    # Run the planar version of the code on the planar images and the
    # interleaved version of the code on the interleaved
    # images. We'll use a small benchmarking utility, which takes a function
    # to run, the number of batches to run (1 in this case), and the number
    # of iterations per batch (1000 in this case). It returns the best
    # average-iteration time, in seconds.

    planar_time = benchmark(
        samples,
        iterations,
        lambda: brighten_planar.brighten_planar(planar_input, 1, planar_output),
    )
    print(f"brighten_planar: {planar_time * 1e3} msec")

    interleaved_time = benchmark(
        samples,
        iterations,
        lambda: brighten_interleaved.brighten_interleaved(
            interleaved_input, 1, interleaved_output
        ),
    )
    print(f"brighten_interleaved: {interleaved_time * 1e3} msec")

    # Planar is generally faster than interleaved for most imaging
    # operations.
    check_timing(planar_time, interleaved_time)

    # Either of these next two commented-out calls would throw an
    # error, because the stride is not what we promised it would be
    # in the generator.

    # brighten_planar.brighten_planar(interleaved_input, 1, interleaved_output)
    # Error: Constraint violated: brighter.stride.0 (3) == 1 (1)

    # brighten_interleaved.brighten_interleaved(planar_input, 1, planar_output)
    # Error: Constraint violated: brighter.stride.0 (1) == 3 (3)

    # Run the flexible version of the code and check performance. It
    # should work, but it'll be slower than the versions above.
    either_planar_time = benchmark(
        samples,
        iterations,
        lambda: brighten_either.brighten_either(planar_input, 1, planar_output),
    )
    print(f"brighten_either on planar images: {either_planar_time * 1e3} msec")
    check_timing(planar_time, either_planar_time)

    either_interleaved_time = benchmark(
        samples,
        iterations,
        lambda: brighten_either.brighten_either(
            interleaved_input, 1, interleaved_output
        ),
    )
    print(
        f"brighten_either on interleaved images: {either_interleaved_time * 1e3} msec"
    )
    check_timing(interleaved_time, either_interleaved_time)

    # Run the specialized version of the code on each layout. It
    # should match the performance of the code compiled specifically
    # for each case above by branching internally to equivalent
    # code.
    specialized_planar_time = benchmark(
        samples,
        iterations,
        lambda: brighten_specialized.brighten_specialized(
            planar_input, 1, planar_output
        ),
    )
    print(
        f"brighten_specialized on planar images: {specialized_planar_time * 1e3} msec"
    )

    # The cost of the if statement should be negligible, but we'll
    # allow a tolerance of 50% for this test to account for
    # measurement noise.
    check_timing(specialized_planar_time, 1.5 * planar_time)

    specialized_interleaved_time = benchmark(
        samples,
        iterations,
        lambda: brighten_specialized.brighten_specialized(
            interleaved_input, 1, interleaved_output
        ),
    )
    print(
        "brighten_specialized on interleaved images: "
        f"{specialized_interleaved_time * 1e3} msec"
    )
    check_timing(specialized_interleaved_time, 2.0 * interleaved_time)

    return 0


if __name__ == "__main__":
    main()
