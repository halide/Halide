#!/usr/bin/python3

# Halide tutorial lesson 21: Auto-Scheduler: running the compiled pipeline

# Before reading this file, see lesson_21_auto_scheduler_generate.py

# This is the code that actually uses the Halide pipeline we've
# compiled. It does not depend on libHalide, so we won't do
# "import halide".
#
# Instead, it depends on the Python extension modules that
# lesson_21_auto_scheduler_generate produced when we ran it:
import random
import time

import auto_schedule_false
import auto_schedule_true

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


def main():
    # Let's declare and initialize the input images
    input = hl.Buffer(hl.Float(32), [1024, 1024, 3])

    for c in range(input.channels()):
        for y in range(input.height()):
            for x in range(input.width()):
                input[x, y, c] = random.random()

    output1 = hl.Buffer(hl.Float(32), [1024, 1024])
    output2 = hl.Buffer(hl.Float(32), [1024, 1024])

    # Run each version of the codes (with no auto-schedule and with
    # auto-schedule) multiple times for benchmarking.
    auto_schedule_off = benchmark(
        2,
        5,
        lambda: auto_schedule_false.auto_schedule_false(input, 2.0, output1, output2),
    )
    print(f"Manual schedule: {auto_schedule_off * 1e3}ms")

    auto_schedule_on = benchmark(
        2,
        5,
        lambda: auto_schedule_true.auto_schedule_true(input, 2.0, output1, output2),
    )
    print(f"Auto schedule: {auto_schedule_on * 1e3}ms")

    # auto_schedule_on should be faster since in the auto_schedule_off
    # version, the schedule is very simple.
    if not (auto_schedule_on < auto_schedule_off):
        print(
            "Warning: expected auto_schedule_on < auto_schedule_off, saw "
            f"auto_schedule_on={auto_schedule_on} auto_schedule_off={auto_schedule_off}"
        )

    return 0


if __name__ == "__main__":
    main()
