#!/usr/bin/python3

# Halide tutorial lesson 8

# This lesson demonstrates how schedule multi-stage pipelines.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_08_scheduling_2
# in a shell with the current directory at python_bindings/

import halide as hl
import numpy as np
import math


def main():
    # First we'll declare some Vars to use below.
    x, y = hl.Var("x"), hl.Var("y")

    # Let's examine various scheduling options for a simple two stage
    # pipeline. We'll start with the default schedule:
    if True:
        print("=" * 50)
        producer, consumer = hl.Func("producer_default"), hl.Func("consumer_default")

        # The first stage will be some simple pointwise math similar
        # to our familiar gradient function. The value at position x,
        # y is the sqrt of product of x and y.
        producer[x, y] = hl.sqrt(x * y)

        # Now we'll add a second stage which adds together multiple
        # points in the first stage.
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # We'll turn on tracing for both functions.
        consumer.trace_stores()
        producer.trace_stores()

        # And evaluate it over a 5x5 box.
        print("\nEvaluating producer-consumer pipeline with default schedule")
        consumer.realize([4, 4])

        # There were no messages about computing values of the
        # producer. This is because the default schedule fully
        # inlines 'producer' into 'consumer'. It is as if we had
        # written the following code instead:

        # consumer[x, y] = (sqrt(x * y) +
        #                   sqrt(x * (y + 1)) +
        #                   sqrt((x + 1) * y) +
        #                   sqrt((x + 1) * (y + 1)))

        # All calls to 'producer' have been replaced with the body of
        # 'producer', with the arguments subtituted in for the
        # variables.

        # The equivalent C code is:
        result = np.empty((4, 4), dtype=np.float32)
        for yy in range(4):
            for xx in range(4):
                result[yy][xx] = (
                    math.sqrt(xx * yy)
                    + math.sqrt(xx * (yy + 1))
                    + math.sqrt((xx + 1) * yy)
                    + math.sqrt((xx + 1) * (yy + 1))
                )

        print()

        # If we look at the loop nest, the producer doesn't appear
        # at all. It has been inlined into the consumer.
        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

    # Next we'll examine the next simplest option - computing all
    # values required in the producer before computing any of the
    # consumer. We call this schedule "root".
    if True:
        print("=" * 50)
        # Start with the same function definitions:
        producer, consumer = hl.Func("producer_root"), hl.Func("consumer_root")
        producer[x, y] = hl.sqrt(x * y)
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # Tell Halide to evaluate all of producer before any of consumer.
        producer.compute_root()

        # Turn on tracing.
        consumer.trace_stores()
        producer.trace_stores()

        # Compile and run.
        print("\nEvaluating producer.compute_root()")
        consumer.realize([4, 4])

        # Reading the output we can see that:
        # A) There were stores to producer.
        # B) They all happened before any stores to consumer.

        # Equivalent C:
        result = np.empty((4, 4), dtype=np.float32)

        # Allocate some temporary storage for the producer.
        producer_storage = np.empty((5, 5), dtype=np.float32)

        # Compute the producer.
        for yy in range(5):
            for xx in range(5):
                producer_storage[yy][xx] = math.sqrt(xx * yy)

        # Compute the consumer. Skip the prints this time.
        for yy in range(4):
            for xx in range(4):
                result[yy][xx] = (
                    producer_storage[yy][xx]
                    + producer_storage[yy + 1][xx]
                    + producer_storage[yy][xx + 1]
                    + producer_storage[yy + 1][xx + 1]
                )

        # Note that consumer was evaluated over a 4x4 box, so Halide
        # automatically inferred that producer was needed over a 5x5
        # box. This is the same 'bounds inference' logic we saw in
        # the previous lesson, where it was used to detect and avoid
        # out-of-bounds reads from an input image.

        # If we print the loop nest, we'll see something very
        # similar to the C above.
        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

    # Let's compare the two approaches above from a performance
    # perspective.

    # Full inlining (the default schedule):
    # - Temporary memory allocated: 0
    # - Loads: 0
    # - Stores: 16
    # - Calls to sqrt: 64

    # producer.compute_root():
    # - Temporary memory allocated: 25 floats
    # - Loads: 64
    # - Stores: 39
    # - Calls to sqrt: 25

    # There's a trade-off here. Full inlining used minimal temporary
    # memory and memory bandwidth, but did a whole bunch of redundant
    # expensive math (calling sqrt). It evaluated most points in
    # 'producer' four times. The second schedule,
    # producer.compute_root(), did the mimimum number of calls to
    # sqrt, but used more temporary memory and more memory bandwidth.

    # In any given situation the correct choice can be difficult to
    # make. If you're memory-bandwidth limited, or don't have much
    # memory (e.g. because you're running on an old cell-phone), then
    # it can make sense to do redundant math. On the other hand, sqrt
    # is expensive, so if you're compute-limited then fewer calls to
    # sqrt will make your program faster. Adding vectorization or
    # multi-core parallelism tilts the scales in favor of doing
    # redundant work, because firing up multiple cpu cores increases
    # the amount of math you can do per second, but doesn't increase
    # your system memory bandwidth or capacity.

    # We can make choices in between full inlining and
    # compute_root. Next we'll alternate between computing the
    # producer and consumer on a per-scanline basis:
    if True:
        print("=" * 50)
        # Start with the same function definitions:
        producer, consumer = hl.Func("producer_y"), hl.Func("consumer_y")
        producer[x, y] = hl.sqrt(x * y)
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # Tell Halide to evaluate producer as needed per y coordinate
        # of the consumer:
        producer.compute_at(consumer, y)

        # This places the code that computes the producer just
        # *inside* the consumer's for loop over y, as in the
        # equivalent C below.

        # Turn on tracing.
        producer.trace_stores()
        consumer.trace_stores()

        # Compile and run.
        print("\nEvaluating producer.compute_at(consumer, y)")
        consumer.realize([4, 4])

        # Reading the log you should see that producer and consumer
        # alternate on a per-scanline basis. Let's look at the
        # equivalent C:
        result = np.empty((4, 4), dtype=np.float32)

        # There's an outer loop over scanlines of consumer:
        for yy in range(4):
            # Allocate space and compute enough of the producer to
            # satisfy this single scanline of the consumer. This
            # means a 5x2 box of the producer.
            producer_storage = np.empty((2, 5), dtype=np.float32)
            for py in range(yy, yy + 2):
                for px in range(5):
                    producer_storage[py - yy][px] = math.sqrt(px * py)

            # Compute a scanline of the consumer.
            for xx in range(4):
                result[yy][xx] = (
                    producer_storage[0][xx]
                    + producer_storage[1][xx]
                    + producer_storage[0][xx + 1]
                    + producer_storage[1][xx + 1]
                )

        # Again, if we print the loop nest, we'll see something very
        # similar to the C above.
        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

        # The performance characteristics of this strategy are in
        # between inlining and compute root. We still allocate some
        # temporary memory, but less that compute_root, and with
        # better locality (we load from it soon after writing to it,
        # so for larger images, values should still be in cache). We
        # still do some redundant work, but less than full inlining:

        # producer.compute_at(consumer, y):
        # - Temporary memory allocated: 10 floats
        # - Loads: 64
        # - Stores: 56
        # - Calls to sqrt: 40

    # We could also say producer.compute_at(consumer, x), but this
    # would be very similar to full inlining (the default
    # schedule). Instead let's distinguish between the loop level at
    # which we allocate storage for producer, and the loop level at
    # which we actually compute it. This unlocks a few optimizations.
    if True:
        print("=" * 50)
        producer = hl.Func("producer_store_root_compute_y")
        consumer = hl.Func("consumer_store_root_compute_y")
        producer[x, y] = hl.sqrt(x * y)
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # Tell Halide to make a buffer to store all of producer at
        # the outermost level:
        producer.store_root()
        # ... but compute it as needed per y coordinate of the
        # consumer.
        producer.compute_at(consumer, y)

        producer.trace_stores()
        consumer.trace_stores()

        print("\nEvaluating producer.store_root().compute_at(consumer, y)")
        consumer.realize([4, 4])

        # Reading the log you should see that producer and consumer
        # again alternate on a per-scanline basis. It computes a 5x2
        # box of the producer to satisfy the first scanline of the
        # consumer, but after that it only computes a 5x1 box of the
        # output for each new scanline of the consumer!
        #
        # Halide has detected that for all scanlines except for the
        # first, it can reuse the values already sitting in the
        # buffer we've allocated for producer. Let's look at the
        # equivalent C:

        result = np.empty((4, 4), dtype=np.float32)

        # producer.store_root() implies that storage goes here:
        producer_storage = np.empty((5, 5), dtype=np.float32)

        # There's an outer loop over scanlines of consumer:
        for yy in range(4):
            # Compute enough of the producer to satisfy this scanline
            # of the consumer.
            for py in range(yy, yy + 2):
                # Skip over rows of producer that we've already
                # computed in a previous iteration.
                if yy > 0 and py == yy:
                    continue

                for px in range(5):
                    producer_storage[py][px] = math.sqrt(px * py)

            # Compute a scanline of the consumer.
            for xx in range(4):
                result[yy][xx] = (
                    producer_storage[yy][xx]
                    + producer_storage[yy + 1][xx]
                    + producer_storage[yy][xx + 1]
                    + producer_storage[yy + 1][xx + 1]
                )

        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

        # The performance characteristics of this strategy are pretty
        # good! The numbers are similar compute_root, except locality
        # is better. We're doing the minimum number of sqrt calls,
        # and we load values soon after they are stored, so we're
        # probably making good use of the cache:

        # producer.store_root().compute_at(consumer, y):
        # - Temporary memory allocated: 10 floats
        # - Loads: 64
        # - Stores: 39
        # - Calls to sqrt: 25

        # Note that my claimed amount of memory allocated doesn't
        # match the reference C code. Halide is performing one more
        # optimization under the hood. It folds the storage for the
        # producer down into a circular buffer of two
        # scanlines. Equivalent C would actually look like this:

        if True:
            # Actually store 2 scanlines instead of 5
            producer_storage = np.empty((2, 5), dtype=np.float32)

            for yy in range(4):
                for py in range(yy, yy + 2):
                    if yy > 0 and py == yy:
                        continue

                    for px in range(5):
                        # Stores to producer_storage have their y coordinate
                        # bit-masked.
                        producer_storage[py & 1][px] = math.sqrt(px * py)

                # Compute a scanline of the consumer.
                for xx in range(4):
                    # Loads from producer_storage have their y coordinate
                    # bit-masked.
                    result[yy][xx] = (
                        producer_storage[yy & 1][xx]
                        + producer_storage[(yy + 1) & 1][xx]
                        + producer_storage[yy & 1][xx + 1]
                        + producer_storage[(yy + 1) & 1][xx + 1]
                    )

    # We can do even better, by leaving the storage outermost, but
    # moving the computation into the innermost loop:
    if True:
        print("=" * 50)
        producer = hl.Func("producer_store_root_compute_x")
        consumer = hl.Func("consumer_store_root_compute_x")
        producer[x, y] = hl.sqrt(x * y)
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # Store outermost, compute innermost.
        producer.store_root().compute_at(consumer, x)

        producer.trace_stores()
        consumer.trace_stores()

        print("\nEvaluating producer.store_root().compute_at(consumer, x)")
        consumer.realize([4, 4])

        # Reading the log, you should see that producer and consumer
        # now alternate on a per-pixel basis. Here's the equivalent C:

        result = np.empty((4, 4), dtype=np.float32)

        # producer.store_root() implies that storage goes here, but
        # we can fold it down into a circular buffer of two
        # scanlines:
        producer_storage = np.empty((2, 5), dtype=np.float32)

        # For every pixel of the consumer:
        for yy in range(4):
            for xx in range(4):
                # Compute enough of the producer to satisfyy this
                # pixxel of the consumer, but skip values that we've
                # alreadyy computed:
                if (yy == 0) and (xx == 0):
                    producer_storage[yy & 1][xx] = math.sqrt(xx * yy)
                if yy == 0:
                    producer_storage[yy & 1][xx + 1] = math.sqrt((xx + 1) * yy)
                if xx == 0:
                    producer_storage[(yy + 1) & 1][xx] = math.sqrt(xx * (yy + 1))

                producer_storage[(yy + 1) & 1][xx + 1] = math.sqrt((xx + 1) * (yy + 1))

                result[yy][xx] = (
                    producer_storage[yy & 1][xx]
                    + producer_storage[(yy + 1) & 1][xx]
                    + producer_storage[yy & 1][xx + 1]
                    + producer_storage[(yy + 1) & 1][xx + 1]
                )

        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

        # The performance characteristics of this strategy are the
        # best so far. One of the four values of the producer we need
        # is probably still sitting in a register, so I won't count
        # it as a load:
        # producer.store_root().compute_at(consumer, x):
        # - Temporary memory allocated: 10 floats
        # - Loads: 48
        # - Stores: 56
        # - Calls to sqrt: 40

    # So what's the catch? Why not always do
    # producer.store_root().compute_at(consumer, x) for this type of
    # code?
    #
    # The answer is parallelism. In both of the previous two
    # strategies we've assumed that values computed on previous
    # iterations are lying around for us to reuse. This assumes that
    # previous values of x or y happened earlier in time and have
    # finished. This is not true if you parallelize or vectorize
    # either loop. Darn. If you parallelize, Halide won't inject the
    # optimizations that skip work already done if there's a parallel
    # loop in between the store_at level and the compute_at level,
    # and won't fold the storage down into a circular buffer either,
    # which makes our store_root pointless.

    # We're running out of options. We can make new ones by
    # splitting. We can store_at or compute_at at the natural
    # variables of the consumer (x and y), or we can split x or y
    # into new inner and outer sub-variables and then schedule with
    # respect to those. We'll use this to express fusion in tiles:
    if True:
        print("=" * 50)
        producer, consumer = hl.Func("producer_tile"), hl.Func("consumer_tile")
        producer[x, y] = hl.sqrt(x * y)
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # Tile the consumer using 2x2 tiles.
        x_outer, y_outer = hl.Var("x_outer"), hl.Var("y_outer")
        x_inner, y_inner = hl.Var("x_inner"), hl.Var("y_inner")
        consumer.tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2)

        # Compute the producer per tile of the consumer
        producer.compute_at(consumer, x_outer)

        # Notice that I wrote my schedule starting from the end of
        # the pipeline (the consumer). This is because the schedule
        # for the producer refers to x_outer, which we introduced
        # when we tiled the consumer. You can write it in the other
        # order, but it tends to be harder to read.

        # Turn on tracing.
        producer.trace_stores()
        consumer.trace_stores()

        print(
            "\nEvaluating:"
            "consumer.tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2)"
            "producer.compute_at(consumer, x_outer)"
        )
        consumer.realize([4, 4])

        # Reading the log, you should see that producer and consumer
        # now alternate on a per-tile basis. Here's the equivalent C:

        result = np.empty((4, 4), dtype=np.float32)

        # For every tile of the consumer:
        for y_outer in range(2):
            for x_outer in range(2):
                # Compute the x and y coords of the start of this tile.
                x_base = x_outer * 2
                y_base = y_outer * 2

                # Compute enough of producer to satisfy this tile. A
                # 2x2 tile of the consumer requires a 3x3 tile of the
                # producer.
                producer_storage = np.empty((3, 3), dtype=np.float32)
                for py in range(y_base, y_base + 3):
                    for px in range(x_base + 3):
                        producer_storage[py - y_base][px - x_base] = math.sqrt(px * py)

                # Compute this tile of the consumer
                for y_inner in range(2):
                    for x_inner in range(2):
                        xx = x_base + x_inner
                        yy = y_base + y_inner
                        result[yy][xx] = (
                            producer_storage[yy - y_base][xx - x_base]
                            + producer_storage[yy - y_base + 1][xx - x_base]
                            + producer_storage[yy - y_base][xx - x_base + 1]
                            + producer_storage[yy - y_base + 1][xx - x_base + 1]
                        )

        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

        # Tiling can make sense for problems like this one with
        # stencils that reach outwards in x and y. Each tile can be
        # computed independently in parallel, and the redundant work
        # done by each tile isn't so bad once the tiles get large
        # enough.

    # Let's try a mixed strategy that combines what we have done with
    # splitting, parallelizing, and vectorizing. This is one that
    # often works well in practice for large images. If you
    # understand this schedule, then you understand 95% of scheduling
    # in Halide.
    if True:
        print("=" * 50)
        producer, consumer = hl.Func("producer_mixed"), hl.Func("consumer_mixed")
        producer[x, y] = hl.sqrt(x * y)
        consumer[x, y] = (
            producer[x, y]
            + producer[x, y + 1]
            + producer[x + 1, y]
            + producer[x + 1, y + 1]
        )

        # Split the y coordinate of the consumer into strips of 16 scanlines:
        yo, yi = hl.Var("yo"), hl.Var("yi")
        consumer.split(y, yo, yi, 16)
        # Compute the strips using a thread pool and a task queue.
        consumer.parallel(yo)
        # Vectorize across x by a factor of four.
        consumer.vectorize(x, 4)

        # Now store the producer per-strip. This will be 17 scanlines
        # of the producer (16+1), but hopefully it will fold down
        # into a circular buffer of two scanlines:
        producer.store_at(consumer, yo)
        # Within each strip, compute the producer per scanline of the
        # consumer, skipping work done on previous scanlines.
        producer.compute_at(consumer, yi)
        # Also vectorize the producer (because sqrt is vectorizable on x86
        # using SSE).
        producer.vectorize(x, 4)

        # Let's leave tracing off this time, because we're going to
        # evaluate over a larger image.
        # consumer.trace_stores()
        # producer.trace_stores()

        halide_result = consumer.realize([800, 600])

        # Here's the equivalent (serial) C:

        c_result = np.empty((600, 800), dtype=np.float32)

        # For every strip of 16 scanlines
        for yo in range(600 // 16 + 1):  # (this loop is parallel in the Halide version)
            # 16 doesn't divide 600, so push the last slice upwards to fit
            # within [0, 599] (see lesson 05).
            y_base = yo * 16
            if y_base > (600 - 16):
                y_base = 600 - 16

            # Allocate a two-scanline circular buffer for the producer
            producer_storage = np.empty((2, 801), dtype=np.float32)

            # For every scanline in the strip of 16:
            for yi in range(16):
                yy = y_base + yi

                for py in range(yy, yy + 2):
                    # Skip scanlines already computed *within this task*
                    if (yi > 0) and (py == yy):
                        continue

                    # Compute this scanline of the producer in 4-wide vectors
                    for x_vec in range(800 // 4 + 1):
                        x_base = x_vec * 4
                        # 4 doesn't divide 801, so push the last vector left
                        # (see lesson 05).
                        if x_base > (801 - 4):
                            x_base = 801 - 4

                        # If you're on x86, Halide generates SSE code for this
                        # part:
                        xx = [x_base + 0, x_base + 1, x_base + 2, x_base + 3]
                        vec = [
                            math.sqrt(xx[0] * py),
                            math.sqrt(xx[1] * py),
                            math.sqrt(xx[2] * py),
                            math.sqrt(xx[3] * py),
                        ]
                        producer_storage[py & 1][xx[0]] = vec[0]
                        producer_storage[py & 1][xx[1]] = vec[1]
                        producer_storage[py & 1][xx[2]] = vec[2]
                        producer_storage[py & 1][xx[3]] = vec[3]

                # Now compute consumer for this scanline:
                for x_vec in range(800 // 4):
                    x_base = x_vec * 4
                    # Again, Halide's equivalent here uses SSE.
                    xx = [x_base, x_base + 1, x_base + 2, x_base + 3]
                    vec = [
                        (
                            producer_storage[yy & 1][xx[0]]
                            + producer_storage[(yy + 1) & 1][xx[0]]
                            + producer_storage[yy & 1][xx[0] + 1]
                            + producer_storage[(yy + 1) & 1][xx[0] + 1]
                        ),
                        (
                            producer_storage[yy & 1][xx[1]]
                            + producer_storage[(yy + 1) & 1][xx[1]]
                            + producer_storage[yy & 1][xx[1] + 1]
                            + producer_storage[(yy + 1) & 1][xx[1] + 1]
                        ),
                        (
                            producer_storage[yy & 1][xx[2]]
                            + producer_storage[(yy + 1) & 1][xx[2]]
                            + producer_storage[yy & 1][xx[2] + 1]
                            + producer_storage[(yy + 1) & 1][xx[2] + 1]
                        ),
                        (
                            producer_storage[yy & 1][xx[3]]
                            + producer_storage[(yy + 1) & 1][xx[3]]
                            + producer_storage[yy & 1][xx[3] + 1]
                            + producer_storage[(yy + 1) & 1][xx[3] + 1]
                        ),
                    ]

                    c_result[yy][xx[0]] = vec[0]
                    c_result[yy][xx[1]] = vec[1]
                    c_result[yy][xx[2]] = vec[2]
                    c_result[yy][xx[3]] = vec[3]

        print("Pseudo-code for the schedule:")
        consumer.print_loop_nest()
        print()

        # Look on my code, ye mighty, and despair!

        # Let's check the C result against the Halide result. Doing
        # this I found several bugs in my C implementation, which
        # should tell you something.
        for yy in range(600):
            for xx in range(800):
                error = halide_result[xx, yy] - c_result[yy][xx]
                # It's floating-point math, so we'll allow some slop:
                assert abs(error) <= 0.001, (
                    f"halide_result({xx}, {yy}) = {halide_result[xx, yy]} instead of {c_result[yy][xx]}"
                )

    # This stuff is hard. We ended up in a three-way trade-off
    # between memory bandwidth, redundant work, and
    # parallelism. Halide can't make the correct choice for you
    # automatically (sorry). Instead it tries to make it easier for
    # you to explore various options, without messing up your
    # program. In fact, Halide promises that scheduling calls like
    # compute_root won't change the meaning of your algorithm -- you
    # should get the same bits back no matter how you schedule
    # things.

    # So be empirical! Experiment with various schedules and keep a
    # log of performance. Form hypotheses and then try to prove
    # yourself wrong. Don't assume that you just need to vectorize
    # your code by a factor of four and run it on eight cores and
    # you'll get 32x faster. This almost never works. Modern systems
    # are complex enough that you can't predict performance reliably
    # without running your code.

    # We suggest you start by scheduling all of your non-trivial
    # stages compute_root, and then work from the end of the pipeline
    # upwards, inlining, parallelizing, and vectorizing each stage in
    # turn until you reach the top.

    # Halide is not just about vectorizing and parallelizing your
    # code. That's not enough to get you very far. Halide is about
    # giving you tools that help you quickly explore different
    # trade-offs between locality, redundant work, and parallelism,
    # without messing up the actual result you're trying to compute.

    print("=" * 50)
    print("Success!")
    return 0


if __name__ == "__main__":
    main()
