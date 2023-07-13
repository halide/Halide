#!/usr/bin/python3

# Halide tutorial lesson 9

# This lesson demonstrates how to define a hl.Func in multiple passes,
# including scattering.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_09_update_definitions
# in a shell with the current directory at python_bindings/
from datetime import datetime

import halide as hl

import halide.imageio
import numpy as np
import os.path


def main():
    # Declare some Vars to use below.
    x, y = hl.Var("x"), hl.Var("y")

    # Load a grayscale image to use as an input.
    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/gray.png")
    input_data = halide.imageio.imread(image_path)
    if True:
         # making the image smaller to go faster
        input_data = input_data[:150, :160]
    assert input_data.dtype == np.uint8
    input = hl.Buffer(input_data)

    # You can define a hl.Func in multiple passes. Let's see a toy
    # example first.
    if True:
        # The first definition must be one like we have seen already
        # - a mapping from Vars to an hl.Expr:
        f = hl.Func("f")
        f[x, y] = x + y
        # We call this first definition the "pure" definition.

        # But the later definitions can include computed expressions on
        # both sides. The simplest example is modifying a single point:
        f[3, 7] = 42

        # We call these extra definitions "update" definitions, or
        # "reduction" definitions. A reduction definition is an
        # update definition that recursively refers back to the
        # function's current value at the same site:
        if False:
            e = f[x, y] + 17
            print("f[x, y] + 17", e)
            print("(f[x, y] + 17).type()", e.type())
            print("(f[x, y]).type()", f[x, y].type())

        f[x, y] = f[x, y] + 17

        # If we confine our update to a single row, we can
        # recursively refer to values in the same column:
        f[x, 3] = f[x, 0] * f[x, 10]

        # Similarly, if we confine our update to a single column, we
        # can recursively refer to other values in the same row.
        f[0, y] = f[0, y] / f[3, y]

        # The general rule is: Each hl.Var used in an update definition
        # must appear unadorned in the same position as in the pure
        # definition in all references to the function on the left-
        # and right-hand sides. So the following definitions are
        # legal updates:
        # x is used, so all uses of f must have x as the first argument.
        f[x, 17] = x + 8
        # y is used, so all uses of f must have y as the second argument.
        f[0, y] = y * 8
        f[x, x + 1] = x + 8
        f[y / 2, y] = f[0, y] * 17

        # But these ones would cause an error:
        # f[x, 0) = f[x + 1, 0) <- First argument to f on the right-hand-side must be 'x', not 'x + 1'.
        # f[y, y + 1) = y + 8   <- Second argument to f on the left-hand-side must be 'y', not 'y + 1'.
        # f[y, x) = y - x      <- Arguments to f on the left-hand-side are in the wrong places.
        # f[3, 4) = x + y      <- Free variables appear on the right-hand-side
        # but not the left-hand-side.

        # We'll realize this one just to make sure it compiles. The
        # second-to-last definition forces us to realize over a
        # domain that is taller than it is wide.
        f.realize([100, 101])

        # For each realization of f, each step runs in its entirety
        # before the next one begins. Let's trace the loads and
        # stores for a simpler example:
        g = hl.Func("g")
        g[x, y] = x + y   # Pure definition
        g[2, 1] = 42      # First update definition
        g[x, 0] = g[x, 1]  # Second update definition

        g.trace_loads()
        g.trace_stores()

        g.realize([4, 4])

        # Reading the log, we see that each pass is applied in turn. The
        # equivalent Python is:
        result = np.empty((4, 4), dtype=np.int32)
        # Pure definition
        for yy in range(4):
            for xx in range(4):
                result[yy][xx] = xx + yy

        # First update definition
        result[1][2] = 42
        # Second update definition
        for xx in range(4):
            result[0][xx] = result[1][xx]
    # end of section

    # Putting update passes inside loops.
    if True:
        # Starting with this pure definition:
        f = hl.Func("f")
        f[x, y] = x + y

        # Say we want an update that squares the first fifty rows. We
        # could do this by adding 50 update definitions:

        # f[x, 0) = f[x, 0) * f[x, 0)
        # f[x, 1) = f[x, 1) * f[x, 1)
        # f[x, 2) = f[x, 2) * f[x, 2)
        # ...
        # f[x, 49) = f[x, 49) * f[x, 49)

        # Or equivalently using a compile-time loop in our C++:
        # for (int i = 0 i < 50 i++) {
        #   f[x, i) = f[x, i) * f[x, i)
        #

        # But it's more manageable and more flexible to put the loop
        # in the generated code. We do this by defining a "reduction
        # domain" and using it inside an update definition:
        r = hl.RDom([(0, 50)])
        f[x, r] = f[x, r] * f[x, r]
        halide_result = f.realize([100, 100])

        # The equivalent Python is:
        py_result = np.empty((100, 100), dtype=np.int32)
        for yy in range(100):
            for xx in range(100):
                py_result[yy][xx] = xx + yy

        for xx in range(100):
            for rr in range(50):
                # The loop over the reduction domain occurs inside of
                # the loop over any pure variables used in the update
                # step:
                py_result[rr][xx] = py_result[rr][xx] * py_result[rr][xx]

        # Check the results match:
        for yy in range(100):
            for xx in range(100):
                assert halide_result[xx, yy] == py_result[yy][xx], \
                    "halide_result(%d, %d) = %d instead of %d" % (
                        xx, yy, halide_result[xx, yy], py_result[yy][xx])

    # Now we'll examine a real-world use for an update definition:
    # computing a histogram.
    if True:

        # Some operations on images can't be cleanly expressed as a pure
        # function from the output coordinates to the value stored
        # there. The classic example is computing a histogram. The
        # natural way to do it is to iterate over the input image,
        # updating histogram buckets. Here's how you do that in Halide:
        histogram = hl.Func("histogram")

        # Histogram buckets start as zero.
        histogram[x] = 0

        # Define a multi-dimensional reduction domain over the input image:
        r = hl.RDom([(0, input.width()), (0, input.height())])

        # For every point in the reduction domain, increment the
        # histogram bucket corresponding to the intensity of the
        # input image at that point.
        histogram[input[r.x, r.y]] += 1

        halide_result = histogram.realize([256])

        # The equivalent Python is:
        py_result = np.empty((256), dtype=np.int32)
        for xx in range(256):
            py_result[xx] = 0

        for r_y in range(input.height()):
            for r_x in range(input.width()):
                py_result[input_data[r_y, r_x]] += 1

        # Check the answers agree:
        for xx in range(256):
            assert py_result[xx] == halide_result[xx], \
                "halide_result(%d) = %d instead of %d" % (xx, halide_result[xx], py_result[xx])

    # Scheduling update steps
    if True:
        # The pure variables in an update step and can be
        # parallelized, vectorized, split, etc as usual.

        # Vectorizing, splitting, or parallelize the variables that
        # are part of the reduction domain is trickier. We'll cover
        # that in a later lesson.

        # Consider the definition:
        f = hl.Func("x")
        f[x, y] = x * y
        # Set the second row to equal the first row.
        f[x, 1] = f[x, 0]
        # Set the second column to equal the first column plus 2.
        f[1, y] = f[0, y] + 2

        # The pure variables in each stage can be scheduled
        # independently. To control the pure definition, we schedule
        # as we have done in the past. The following code vectorizes
        # and parallelizes the pure definition only.
        f.vectorize(x, 4).parallel(y)

        # We use hl.Func::update(int) to get a handle to an update step
        # for the purposes of scheduling. The following line
        # vectorizes the first update step across x. We can't do
        # anything with y for this update step, because it doesn't
        # use y.
        f.update(0).vectorize(x, 4)

        # Now we parallelize the second update step in chunks of size
        # 4.
        yo, yi = hl.Var("yo"), hl.Var("yi")
        f.update(1).split(y, yo, yi, 4).parallel(yo)

        halide_result = f.realize([16, 16])

        # Here's the equivalent (serial) C:
        py_result = np.empty((16, 16), dtype=np.int32)

        # Pure step. Vectorized in x and parallelized in y.
        for yy in range(16):  # Should be a parallel for loop
            for x_vec in range(4):
                xx = [x_vec * 4, x_vec * 4 + 1, x_vec * 4 + 2, x_vec * 4 + 3]
                py_result[yy][xx[0]] = xx[0] * yy
                py_result[yy][xx[1]] = xx[1] * yy
                py_result[yy][xx[2]] = xx[2] * yy
                py_result[yy][xx[3]] = xx[3] * yy

        # First update. Vectorized in x.
        for x_vec in range(4):
            xx = [x_vec * 4, x_vec * 4 + 1, x_vec * 4 + 2, x_vec * 4 + 3]
            py_result[1][xx[0]] = py_result[0][xx[0]]
            py_result[1][xx[1]] = py_result[0][xx[1]]
            py_result[1][xx[2]] = py_result[0][xx[2]]
            py_result[1][xx[3]] = py_result[0][xx[3]]

        # Second update. Parallelized in chunks of size 4 in y.
        for yo in range(4):  # Should be a parallel for loop
            for yi in range(4):
                yy = yo * 4 + yi
                py_result[yy][1] = py_result[yy][0] + 2

        # Check the C and Halide results match:
        for yy in range(16):
            for xx in range(16):
                assert halide_result[xx, yy] == py_result[yy][xx], \
                    "halide_result(%d, %d) = %d instead of %d" % (
                        xx, yy, halide_result[xx, yy], py_result[yy][xx])

    # That covers how to schedule the variables within a hl.Func that
    # uses update steps, but what about producer-consumer
    # relationships that involve compute_at and store_at? Let's
    # examine a reduction as a producer, in a producer-consumer pair.
    if True:
        # Because an update does multiple passes over a stored array,
        # it's not meaningful to inline them. So the default schedule
        # for them does the closest thing possible. It computes them
        # in the innermost loop of their consumer. Consider this
        # trivial example:
        producer, consumer = hl.Func("producer"), hl.Func("consumer")
        producer[x] = x * 17
        producer[x] += 1
        consumer[x] = 2 * producer[x]
        halide_result = consumer.realize([10])

        # The equivalent Python is:
        py_result = np.empty((10), dtype=np.int32)
        for xx in range(10):
            producer_storage = np.empty((1), dtype=np.int32)
            # Pure step for producer
            producer_storage[0] = xx * 17
            # Update step for producer
            producer_storage[0] = producer_storage[0] + 1
            # Pure step for consumer
            py_result[xx] = 2 * producer_storage[0]

        # Check the results match
        for xx in range(10):
            assert halide_result[xx] == py_result[xx], \
                "halide_result(%d) = %d instead of %d" % (xx, halide_result[xx], py_result[xx])

        # For all other compute_at/store_at options, the reduction
        # gets placed where you would expect, somewhere in the loop
        # nest of the consumer.

    # Now let's consider a reduction as a consumer in a
    # producer-consumer pair. This is a little more involved.
    if True:
        if True:
            # Case 1: The consumer references the producer in the pure step
            # only.
            producer, consumer = hl.Func("producer"), hl.Func("consumer")
            # The producer is pure.
            producer[x] = x * 17
            consumer[x] = 2 * producer[x]
            consumer[x] += 1

            # The valid schedules for the producer in this case are
            # the default schedule - inlined, and also:
            #
            # 1) producer.compute_at(x), which places the computation of
            # the producer inside the loop over x in the pure step of the
            # consumer.
            #
            # 2) producer.compute_root(), which computes all of the
            # producer ahead of time.
            #
            # 3) producer.store_root().compute_at(x), which allocates
            # space for the consumer outside the loop over x, but fills
            # it in as needed inside the loop.
            #
            # Let's use option 1.

            producer.compute_at(consumer, x)

            halide_result = consumer.realize([10])

            # The equivalent Python is:
            py_result = np.empty((10), dtype=np.int32)

            # Pure step for the consumer
            for xx in range(10):
                # Pure step for producer
                producer_storage = np.empty((1), dtype=np.int32)
                producer_storage[0] = xx * 17
                py_result[xx] = 2 * producer_storage[0]

            # Update step for the consumer
            for xx in range(10):
                py_result[xx] += 1

            # All of the pure step is evaluated before any of the
            # update step, so there are two separate loops over x.

            # Check the results match
            for xx in range(10):
                assert halide_result[xx] == py_result[xx], \
                    "halide_result(%d) = %d instead of %d" % (xx, halide_result[xx], py_result[xx])

        if True:
            # Case 2: The consumer references the producer in the update step
            # only
            producer, consumer = hl.Func("producer"), hl.Func("consumer")
            producer[x] = x * 17
            consumer[x] = x
            consumer[x] += producer[x]

            # Again we compute the producer per x coordinate of the
            # consumer. This places producer code inside the update
            # step of the producer, because that's the only step that
            # uses the producer.
            producer.compute_at(consumer, x)

            # Note however, that we didn't say:
            #
            # producer.compute_at(consumer.update(0), x).
            #
            # Scheduling is done with respect to Vars of a hl.Func, and
            # the Vars of a hl.Func are shared across the pure and
            # update steps.

            halide_result = consumer.realize([10])

            # The equivalent Python is:
            py_result = np.empty((10), dtype=np.int32)
            # Pure step for the consumer
            for xx in range(10):
                py_result[xx] = xx

            # Update step for the consumer
            for xx in range(10):
                # Pure step for producer
                producer_storage = np.empty((1), dtype=np.int32)
                producer_storage[0] = xx * 17
                py_result[xx] += producer_storage[0]

            # Check the results match
            for xx in range(10):
                assert halide_result[xx] == py_result[xx], \
                    "halide_result(%d) = %d instead of %d" % (xx, halide_result[xx], py_result[xx])

        if True:
            # Case 3: The consumer references the producer in
            # multiple steps that share common variables
            producer, consumer = hl.Func("producer"), hl.Func("consumer")
            producer[x] = x * 17
            consumer[x] = producer[x] * x
            consumer[x] += producer[x]

            # Again we compute the producer per x coordinate of the
            # consumer. This places producer code inside both the
            # pure and the update step of the producer. So there ends
            # up being two separate realizations of the producer, and
            # redundant work occurs.
            producer.compute_at(consumer, x)

            halide_result = consumer.realize([10])

            # The equivalent Python is:
            py_result = np.empty((10), dtype=np.int32)
            # Pure step for the consumer
            for xx in range(10):
                # Pure step for producer
                producer_storage = np.empty((1), dtype=np.int32)
                producer_storage[0] = xx * 17
                py_result[xx] = producer_storage[0] * xx

            # Update step for the consumer
            for xx in range(10):
                # Another copy of the pure step for producer
                producer_storage = np.empty((1), dtype=np.int32)
                producer_storage[0] = xx * 17
                py_result[xx] += producer_storage[0]

            # Check the results match
            for xx in range(10):
                assert halide_result[xx] == py_result[xx], \
                    "halide_result(%d) = %d instead of %d" % (xx, halide_result[xx], py_result[xx])

        if True:
            # Case 4: The consumer references the producer in
            # multiple steps that do not share common variables
            producer, consumer = hl.Func("producer"), hl.Func("consumer")
            producer[x, y] = x * y
            consumer[x, y] = x + y
            consumer[x, 0] = producer[x, x - 1]
            consumer[0, y] = producer[y, y - 1]

            # In this case neither producer.compute_at(consumer, x)
            # nor producer.compute_at(consumer, y) will work, because
            # either one fails to cover one of the uses of the
            # producer. So we'd have to inline producer, or use
            # producer.compute_root().

            # Let's say we really really want producer to be
            # compute_at the inner loops of both consumer update
            # steps. Halide doesn't allow multiple different
            # schedules for a single hl.Func, but we can work around it
            # by making two wrappers around producer, and scheduling
            # those instead:

            # Attempt 2:
            producer_wrapper_1, producer_wrapper_2, consumer_2 = hl.Func(), hl.Func(), hl.Func()
            producer_wrapper_1[x, y] = producer[x, y]
            producer_wrapper_2[x, y] = producer[x, y]

            consumer_2[x, y] = x + y
            consumer_2[x, 0] += producer_wrapper_1[x, x - 1]
            consumer_2[0, y] += producer_wrapper_2[y, y - 1]

            # The wrapper functions give us two separate handles on
            # the producer, so we can schedule them differently.
            producer_wrapper_1.compute_at(consumer_2, x)
            producer_wrapper_2.compute_at(consumer_2, y)

            halide_result = consumer_2.realize([10, 10])

            # The equivalent Python is:
            py_result = np.empty((10, 10), dtype=np.int32)

            # Pure step for the consumer
            for yy in range(10):
                for xx in range(10):
                    py_result[yy][xx] = xx + yy

            # First update step for consumer
            for xx in range(10):
                producer_wrapper_1_storage = np.empty((1), dtype=np.int32)
                producer_wrapper_1_storage[0] = xx * (xx - 1)
                py_result[0][xx] += producer_wrapper_1_storage[0]

            # Second update step for consumer
            for yy in range(10):
                producer_wrapper_2_storage = np.empty((1), dtype=np.int32)
                producer_wrapper_2_storage[0] = yy * (yy - 1)
                py_result[yy][0] += producer_wrapper_2_storage[0]

            # Check the results match
            for yy in range(10):
                for xx in range(10):
                    assert halide_result[xx, yy] == py_result[yy][xx], \
                        "halide_result(%d, %d) = %d instead of %d" % (
                            xx, yy, halide_result[xx, yy], py_result[yy][xx])

        if True:
            # Case 5: Scheduling a producer under a reduction domain
            # variable of the consumer.

            # We are not just restricted to scheduling producers at
            # the loops over the pure variables of the consumer. If a
            # producer is only used within a loop over a reduction
            # domain (hl.RDom) variable, we can also schedule the
            # producer there.

            producer, consumer = hl.Func("producer"), hl.Func("consumer")

            r = hl.RDom([(0, 5)])
            producer[x] = x * 17
            consumer[x] = x + 10
            consumer[x] += r + producer[x + r]

            producer.compute_at(consumer, r)

            halide_result = consumer.realize([10])

            # The equivalent Python is:
            py_result = np.empty((10), dtype=np.int32)
            # Pure step for the consumer.
            for xx in range(10):
                py_result[xx] = xx + 10

            # Update step for the consumer.
            for xx in range(10):
                # The loop over the reduction domain is always the inner loop.
                for rr in range(5):
                    # We've schedule the storage and computation of
                    # the producer here. We just need a single value.
                    producer_storage = np.empty((1), dtype=np.int32)
                    # Pure step of the producer.
                    producer_storage[0] = (xx + rr) * 17

                    # Now use it in the update step of the consumer.
                    py_result[xx] += rr + producer_storage[0]

            # Check the results match
            for xx in range(10):
                assert halide_result[xx] == py_result[xx], \
                    "halide_result(%d) = %d instead of %d" % (xx, halide_result[xx], py_result[xx])

    # A real-world example of a reduction inside a producer-consumer chain.
    if True:
        # The default schedule for a reduction is a good one for
        # convolution-like operations. For example, the following
        # computes a 5x5 box-blur of our grayscale test image with a
        # hl.clamp-to-edge boundary condition:

        # First add the boundary condition.
        clamped = hl.BoundaryConditions.repeat_edge(input)

        # Define a 5x5 box that starts at (-2, -2)
        r = hl.RDom([(-2, 5), (-2, 5)])

        # Compute the 5x5 sum around each pixel.
        local_sum = hl.Func("local_sum")
        local_sum[x, y] = 0  # Compute the sum as a 32-bit integer
        local_sum[x, y] += clamped[x + r.x, y + r.y]

        # Divide the sum by 25 to make it an average
        blurry = hl.Func("blurry")
        blurry[x, y] = hl.cast(hl.UInt(8), local_sum[x, y] / 25)

        halide_result = blurry.realize([input.width(), input.height()])

        # The default schedule will inline 'clamped' into the update
        # step of 'local_sum', because clamped only has a pure
        # definition, and so its default schedule is fully-inlined.
        # We will then compute local_sum per x coordinate of blurry,
        # because the default schedule for reductions is
        # compute-innermost. Here's the equivalent Python:

        #cast_to_uint8 = lambda x_: np.array([x_], dtype=np.uint8)[0]
        local_sum = np.empty((1), dtype=np.int32)

        py_result = hl.Buffer(hl.UInt(8), [input.width(), input.height()])
        for yy in range(input.height()):
            for xx in range(input.width()):
                # FIXME this loop is quite slow
                # Pure step of local_sum
                local_sum[0] = 0
                # Update step of local_sum
                for r_y in range(-2, 2 + 1):
                    for r_x in range(-2, 2 + 1):
                        # The clamping has been inlined into the update step.
                        clamped_x = min(max(xx + r_x, 0), input.width() - 1)
                        clamped_y = min(max(yy + r_y, 0), input.height() - 1)
                        local_sum[0] += input[clamped_x, clamped_y]

                # Pure step of blurry
                # py_result(x, y) = (uint8_t)(local_sum[0] / 25)
                #py_result[xx, yy] = cast_to_uint8(local_sum[0] / 25)
                # hl.cast done internally
                py_result[xx, yy] = int(local_sum[0] / 25)

        # Check the results match
        for yy in range(input.height()):
            for xx in range(input.width()):
                assert halide_result[xx, yy] == py_result[xx, yy], \
                    "halide_result(%d, %d) = %d instead of %d" % (
                        xx, yy, halide_result[xx, yy], py_result[xx, yy])

    # Reduction helpers.
    if True:
        # There are several reduction helper functions provided in
        # Halide.h, which compute small reductions and schedule them
        # innermost into their consumer. The most useful one is
        # "sum".
        f1 = hl.Func("f1")
        r = hl.RDom([(0, 100)])
        f1[x] = hl.sum(r + x) * 7

        # Sum creates a small anonymous hl.Func to do the reduction. It's
        # equivalent to:
        f2, anon = hl.Func("f2"), hl.Func("anon")
        anon[x] = 0
        anon[x] += r + x
        f2[x] = anon[x] * 7

        # So even though f1 references a reduction domain, it is a
        # pure function. The reduction domain has been swallowed to
        # define the inner anonymous reduction.
        halide_result_1 = f1.realize([10])
        halide_result_2 = f2.realize([10])

        # The equivalent Python is:
        py_result = np.empty((10), dtype=np.int32)
        for xx in range(10):
            anon = np.empty((1), dtype=np.int32)
            anon[0] = 0
            for rr in range(100):
                anon[0] += rr + xx

            py_result[xx] = anon[0] * 7

        # Check they all match.
        for xx in range(10):
            assert halide_result_1[xx] == py_result[xx], \
                "halide_result_1(%d) = %d instead of %d" % (xx, halide_result_1[xx], py_result[xx])
            assert halide_result_2[xx] == py_result[xx], \
                "halide_result_2(%d) = %d instead of %d" % (xx, halide_result_2[xx], py_result[xx])

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
