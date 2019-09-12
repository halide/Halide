#!/usr/bin/python3
# Halide tutorial lesson 9

# This lesson demonstrates how to define a hl.Func in multiple passes, including scattering.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_09_update_definitions
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_09*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -lpthread -ldl -fopenmp -o lesson_09
# LD_LIBRARY_PATH=../bin ./lesson_09

# On os x (will only work if you actually have g++, not Apple's pretend g++ which is actually clang):
# g++ lesson_09*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -fopenmp -o lesson_09
# DYLD_LIBRARY_PATH=../bin ./lesson_09

#include "Halide.h"
#include <stdio.h>

# We're going to be using x86 SSE intrinsics later on in this lesson.
#ifdef __SSE2__
#include <emmintrin.h>
#endif

# We'll also need a clock to do performance testing at the end.
#include "clock.h"
from datetime import datetime

#using namespace Halide
import halide as hl

# Support code for loading pngs.
#include "image_io.h"
from imageio import imread
import numpy as np
import os.path

def main():
    # Declare some Vars to use below.
    x, y = hl.Var ("x"), hl.Var ("y")

    # Load a grayscale image to use as an input.
    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/gray.png")
    input_data = imread(image_path)
    if True:
         # making the image smaller to go faster
        input_data = input_data[:160, :150]
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
            print("(f[x, y]).type()", f[x,y].type())

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
        f[x, 17] = x + 8 # x is used, so all uses of f must have x as the first argument.
        f[0, y] = y * 8  # y is used, so all uses of f must have y as the second argument.
        f[x, x + 1] = x + 8
        f[y/2, y] = f[0, y] * 17

        # But these ones would cause an error:
        # f[x, 0) = f[x + 1, 0) <- First argument to f on the right-hand-side must be 'x', not 'x + 1'.
        # f[y, y + 1) = y + 8   <- Second argument to f on the left-hand-side must be 'y', not 'y + 1'.
        # f[y, x) = y - x      <- Arguments to f on the left-hand-side are in the wrong places.
        # f[3, 4) = x + y      <- Free variables appear on the right-hand-side but not the left-hand-side.

        # We'll realize this one just to make sure it compiles. The
        # second-to-last definition forces us to realize over a
        # domain that is taller than it is wide.
        f.realize(100, 101)

        # For each realization of f, each step runs in its entirety
        # before the next one begins. Let's trace the loads and
        # stores for a simpler example:
        g = hl.Func("g")
        g[x, y] = x + y   # Pure definition
        g[2, 1] = 42      # First update definition
        g[x, 0] = g[x, 1] # Second update definition

        g.trace_loads()
        g.trace_stores()

        g.realize(4, 4)

        # Reading the log, we see that each pass is applied in turn. The equivalent C is:
        result = np.empty( (4,4), dtype=np.int)
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
        halide_result = f.realize(100, 100)

        # The equivalent C is:
        c_result = np.empty((100, 100), dtype=np.int)
        for yy in range(100):
            for xx in range(100):
                c_result[yy][xx] = xx + yy

        for xx in range(100):
            for rr in range(50):
                # The loop over the reduction domain occurs inside of
                # the loop over any pure variables used in the update
                # step:
                c_result[rr][xx] = c_result[rr][xx] * c_result[rr][xx]



        # Check the results match:
        for yy in range(100):
            for xx in range(100):
                if halide_result[xx, yy] != c_result[yy][xx]:
                    raise Exception("halide_result(%d, %d) = %d instead of %d" % (
                           xx, yy, halide_result[xx, yy], c_result[yy][xx]))
                    return -1





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

        halide_result = histogram.realize(256)

        # The equivalent C is:
        c_result = np.empty((256), dtype=np.int)
        for xx in range(256):
            c_result[xx] = 0

        for r_y in range(input.height()):
            for r_x in range(input.width()):
                c_result[input_data[r_x, r_y]] += 1



        # Check the answers agree:
        for xx in range(256):
            if c_result[xx] != halide_result[xx]:
                raise Exception("halide_result(%d) = %d instead of %d" % (
                       xx, halide_result[xx], c_result[xx]))
                return -1




    # Scheduling update steps
    if True:
        # The pure variables in an update step and can be
        # parallelized, vectorized, split, etc as usual.

        # Vectorizing, splitting, or parallelize the variables that
        # are part of the reduction domain is trickier. We'll cover
        # that in a later lesson.

        # Consider the definition:
        f = hl.Func("x")
        f[x, y] = x*y
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

        halide_result = f.realize(16, 16)


        # Here's the equivalent (serial) C:
        c_result = np.empty((16, 16), dtype=np.int)


        # Pure step. Vectorized in x and parallelized in y.
        for yy in range( 16): # Should be a parallel for loop
            for x_vec in range(4):
                xx = [x_vec*4, x_vec*4+1, x_vec*4+2, x_vec*4+3]
                c_result[yy][xx[0]] = xx[0] * yy
                c_result[yy][xx[1]] = xx[1] * yy
                c_result[yy][xx[2]] = xx[2] * yy
                c_result[yy][xx[3]] = xx[3] * yy



        # First update. Vectorized in x.
        for x_vec in range(4):
            xx = [x_vec*4, x_vec*4+1, x_vec*4+2, x_vec*4+3]
            c_result[1][xx[0]] = c_result[0][xx[0]]
            c_result[1][xx[1]] = c_result[0][xx[1]]
            c_result[1][xx[2]] = c_result[0][xx[2]]
            c_result[1][xx[3]] = c_result[0][xx[3]]


        # Second update. Parallelized in chunks of size 4 in y.
        for yo in range(4): # Should be a parallel for loop
            for yi in range(4):
                yy = yo*4 + yi
                c_result[yy][1] = c_result[yy][0] + 2



        # Check the C and Halide results match:
        for yy in range( 16):
            for xx in range( 16 ):
                if halide_result[xx, yy] != c_result[yy][xx]:
                    raise Exception("halide_result(%d, %d) = %d instead of %d" % (
                           xx, yy, halide_result[xx, yy], c_result[yy][xx]))
                    return -1





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
        producer[x] = x*17
        producer[x] += 1
        consumer[x] = 2 * producer[x]
        halide_result = consumer.realize(10)

        # The equivalent C is:
        c_result = np.empty((10), dtype=np.int)
        for xx in range(10):
            producer_storage = np.empty((1), dtype=np.int)
            # Pure step for producer
            producer_storage[0] = xx * 17
            # Update step for producer
            producer_storage[0] = producer_storage[0] + 1
            # Pure step for consumer
            c_result[xx] = 2 * producer_storage[0]


        # Check the results match
        for xx in range( 10 ):
            if halide_result[xx] != c_result[xx]:
                raise Exception("halide_result(%d) = %d instead of %d" % (
                       xx, halide_result[xx], c_result[xx]))
                return -1



        # For all other compute_at/store_at options, the reduction
        # gets placed where you would expect, somewhere in the loop
        # nest of the consumer.


    # Now let's consider a reduction as a consumer in a
    # producer-consumer pair. This is a little more involved.
    if True:
        if True:
            # Case 1: The consumer references the producer in the pure step only.
            producer, consumer = hl.Func("producer"), hl.Func("consumer")
            # The producer is pure.
            producer[x] = x*17
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

            halide_result = consumer.realize(10)


            # The equivalent C is:
            c_result = np.empty((10), dtype=np.int)

            # Pure step for the consumer
            for xx in range( 10 ):
                # Pure step for producer
                producer_storage = np.empty((1), dtype=np.int)
                producer_storage[0] = xx * 17
                c_result[xx] = 2 * producer_storage[0]

            # Update step for the consumer
            for xx in range( 10 ):
                c_result[xx] += 1


            # All of the pure step is evaluated before any of the
            # update step, so there are two separate loops over x.

            # Check the results match
            for xx in range( 10 ):
                if halide_result[xx] != c_result[xx]:
                    raise Exception("halide_result(%d) = %d instead of %d" % (
                           xx, halide_result[xx], c_result[xx]))
                    return -1




        if True:
            # Case 2: The consumer references the producer in the update step only
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

            halide_result = consumer.realize(10)


            # The equivalent C is:
            c_result = np.empty((10), dtype=np.int)
            # Pure step for the consumer
            for xx in range( 10 ):
                c_result[xx] = xx

            # Update step for the consumer
            for xx in range( 10 ):
                # Pure step for producer
                producer_storage = np.empty((1), dtype=np.int)
                producer_storage[0] = xx * 17
                c_result[xx] += producer_storage[0]



            # Check the results match
            for xx in range( 10 ):
                if halide_result[xx] != c_result[xx]:
                    raise Exception("halide_result(%d) = %d instead of %d" % (
                           xx, halide_result[xx], c_result[xx]))
                    return -1




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

            halide_result = consumer.realize(10)

            # The equivalent C is:
            c_result = np.empty((10), dtype=np.int)
            # Pure step for the consumer
            for xx in range( 10 ):
                # Pure step for producer
                producer_storage = np.empty((1), dtype=np.int)
                producer_storage[0] = xx * 17
                c_result[xx] = producer_storage[0] * xx

            # Update step for the consumer
            for xx in range( 10 ):
                # Another copy of the pure step for producer
                producer_storage = np.empty((1), dtype=np.int)
                producer_storage[0] = xx * 17
                c_result[xx] += producer_storage[0]


            # Check the results match
            for xx in range( 10 ):
                if halide_result[xx] != c_result[xx]:
                    raise Exception("halide_result(%d) = %d instead of %d" % (
                           xx, halide_result[xx], c_result[xx]))
                    return -1




        if True:
            # Case 4: The consumer references the producer in
            # multiple steps that do not share common variables
            producer, consumer = hl.Func("producer"), hl.Func("consumer")
            producer[x, y] = x*y
            consumer[x, y] = x + y
            consumer[x, 0] = producer[x, x-1]
            consumer[0, y] = producer[y, y-1]

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
            consumer_2[x, 0] += producer_wrapper_1[x, x-1]
            consumer_2[0, y] += producer_wrapper_2[y, y-1]

            # The wrapper functions give us two separate handles on
            # the producer, so we can schedule them differently.
            producer_wrapper_1.compute_at(consumer_2, x)
            producer_wrapper_2.compute_at(consumer_2, y)

            halide_result = consumer_2.realize(10, 10)

            # The equivalent C is:
            c_result = np.empty((10, 10), dtype=np.int)

            # Pure step for the consumer
            for yy in range( 10):
                for xx in range( 10 ):
                    c_result[yy][xx] = xx + yy


            # First update step for consumer
            for xx in range( 10 ):
                producer_wrapper_1_storage = np.empty((1), dtype=np.int)
                producer_wrapper_1_storage[0] = xx * (xx-1)
                c_result[0][xx] += producer_wrapper_1_storage[0]

            # Second update step for consumer
            for yy in range( 10):
                producer_wrapper_2_storage = np.empty((1), dtype=np.int)
                producer_wrapper_2_storage[0] = yy * (yy-1)
                c_result[yy][0] += producer_wrapper_2_storage[0]


            # Check the results match
            for yy in range( 10):
                for xx in range( 10 ):
                    if halide_result[xx, yy] != c_result[yy][xx]:
                        print("halide_result(%d, %d) = %d instead of %d",
                               xx, yy, halide_result[xx, yy], c_result[yy][xx])
                        return -1





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

            halide_result = consumer.realize(10)

            # The equivalent C is:
            c_result = np.empty((10), dtype=np.int)
            # Pure step for the consumer.
            for xx in range(10):
                c_result[xx] = xx + 10

            # Update step for the consumer.
            for xx in range( 10 ):
                for rr in range(5): # The loop over the reduction domain is always the inner loop.
                    # We've schedule the storage and computation of
                    # the producer here. We just need a single value.
                    producer_storage = np.empty((1), dtype=np.int)
                    # Pure step of the producer.
                    producer_storage[0] = (xx + rr) * 17

                    # Now use it in the update step of the consumer.
                    c_result[xx] += rr + producer_storage[0]


            # Check the results match
            for xx in range( 10 ):
                if halide_result[xx] != c_result[xx]:
                    raise Exception("halide_result(%d) = %d instead of %d" % (
                           xx, halide_result[xx], c_result[xx]))
                    return -1


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
        local_sum[x, y] = 0 # Compute the sum as a 32-bit integer
        local_sum[x, y] += clamped[x + r.x, y + r.y]

        # Divide the sum by 25 to make it an average
        blurry = hl.Func("blurry")
        blurry[x, y] = hl.cast(hl.UInt(8), local_sum[x, y] / 25)

        halide_result = blurry.realize(input.width(), input.height())

        # The default schedule will inline 'clamped' into the update
        # step of 'local_sum', because clamped only has a pure
        # definition, and so its default schedule is fully-inlined.
        # We will then compute local_sum per x coordinate of blurry,
        # because the default schedule for reductions is
        # compute-innermost. Here's the equivalent C:

        #cast_to_uint8 = lambda x_: np.array([x_], dtype=np.uint8)[0]
        local_sum = np.empty((1), dtype=np.int32)

        c_result = hl.Buffer(hl.UInt(8), [input.width(), input.height()])
        for yy in range(input.height()):
            for xx in range(input.width()):
                # FIXME this loop is quite slow
                # Pure step of local_sum
                local_sum[0] = 0
                # Update step of local_sum
                for r_y in range(-2, 2+1):
                    for r_x in range(-2, 2+1):
                        # The clamping has been inlined into the update step.
                        clamped_x = min(max(xx + r_x, 0), input.width()-1)
                        clamped_y = min(max(yy + r_y, 0), input.height()-1)
                        local_sum[0] += input[clamped_x, clamped_y]

                # Pure step of blurry
                #c_result(x, y) = (uint8_t)(local_sum[0] / 25)
                #c_result[xx, yy] = cast_to_uint8(local_sum[0] / 25)
                c_result[xx, yy] = int(local_sum[0] / 25) # hl.cast done internally

        # Check the results match
        for yy in range(input.height()):
            for xx in range(input.width()):
                if halide_result[xx, yy] != c_result[xx, yy]:
                    raise Exception("halide_result(%d, %d) = %d instead of %d"
                                    % (xx, yy,
                                       halide_result[xx, yy], c_result[xx, yy]))
                    return -1


    # Reduction helpers.
    if True:
        # There are several reduction helper functions provided in
        # Halide.h, which compute small reductions and schedule them
        # innermost into their consumer. The most useful one is
        # "sum".
        f1 = hl.Func ("f1")
        r = hl.RDom([(0, 100)])
        f1[x] = hl.sum(r + x) * 7

        # Sum creates a small anonymous hl.Func to do the reduction. It's equivalent to:
        f2, anon = hl.Func("f2"), hl.Func("anon")
        anon[x] = 0
        anon[x] += r + x
        f2[x] = anon[x] * 7

        # So even though f1 references a reduction domain, it is a
        # pure function. The reduction domain has been swallowed to
        # define the inner anonymous reduction.
        halide_result_1 = f1.realize(10)
        halide_result_2 = f2.realize(10)

        # The equivalent C is:
        c_result = np.empty((10), dtype=np.int)
        for xx in range( 10 ):
            anon = np.empty((1), dtype=np.int)
            anon[0] = 0
            for rr in range(100):
                anon[0] += rr + xx

            c_result[xx] = anon[0] * 7


        # Check they all match.
        for xx in range( 10 ):
            if halide_result_1[xx] != c_result[xx]:
                print("halide_result_1(%d) = %d instead of %d",
                       xx, halide_result_1[xx], c_result[xx])
                return -1

            if halide_result_2[xx] != c_result[xx]:
                print("halide_result_2(%d) = %d instead of %d",
                       xx, halide_result_2[xx], c_result[xx])
                return -1





    # A complex example that uses reduction helpers.
    if False: # non-sense to port SSE code to python, skipping this test

        # Other reduction helpers include "product", "minimum",
        # "maximum", "hl.argmin", and "argmax". Using hl.argmin and argmax
        # requires understanding tuples, which come in a later
        # lesson. Let's use minimum and maximum to compute the local
        # spread of our grayscale image.

        # First, add a boundary condition to the input.
        clamped = hl.Func("clamped")
        x_clamped = hl.clamp(x, 0, input.width()-1)
        y_clamped = hl.clamp(y, 0, input.height()-1)
        clamped[x, y] = input[x_clamped, y_clamped]

        box = hl.RDom([(-2, 5), (-2, 5)])
        # Compute the local maximum minus the local minimum:
        spread = hl.Func("spread")
        spread[x, y] = (maximum(clamped(x + box.x, y + box.y)) -
                        minimum(clamped(x + box.x, y + box.y)))

        # Compute the result in strips of 32 scanlines
        yo, yi = hl.Var("yo"), hl.Var("yi")
        spread.split(y, yo, yi, 32).parallel(yo)

        # Vectorize across x within the strips. This implicitly
        # vectorizes stuff that is computed within the loop over x in
        # spread, which includes our minimum and maximum helpers, so
        # they get vectorized too.
        spread.vectorize(x, 16)

        # We'll apply the boundary condition by padding each scanline
        # as we need it in a circular buffer (see lesson 08).
        clamped.store_at(spread, yo).compute_at(spread, yi)

        halide_result = spread.realize(input.width(), input.height())


        # The C equivalent is almost too horrible to contemplate (and
        # took me a long time to debug). This time I want to time
        # both the Halide version and the C version, so I'll use sse
        # intrinsics for the vectorization, and openmp to do the
        # parallel for loop (you'll need to compile with -fopenmp or
        # similar to get correct timing).
        #ifdef __SSE2__

        # Don't include the time required to allocate the output buffer.
        c_result = hl.Buffer(hl.UInt(8), input.width(), input.height())

        #ifdef _OPENMP
        t1 = datetime.now()
        #endif

        # Run this one hundred times so we can average the timing results.
        for iters in range(100):
            pass
            # #pragma omp parallel for
            # for yo in range((input.height() + 31)/32):
            #     y_base = hl.min(yo * 32, input.height() - 32)
            #
            #     # Compute clamped in a circular buffer of size 8
            #     # (smallest power of two greater than 5). Each thread
            #     # needs its own allocation, so it must occur here.
            #
            #     clamped_width = input.width() + 4
            #     clamped_storage = np.empty((clamped_width * 8), dtype=np.uint8)
            #
            #     for yi in range(32):
            #         y = y_base + yi
            #
            #         uint8_t *output_row = &c_result(0, y)
            #
            #         # Compute clamped for this scanline, skipping rows
            #         # already computed within this slice.
            #         int min_y_clamped = (yi == 0) ? (y - 2) : (y + 2)
            #         int max_y_clamped = (y + 2)
            #         for (int cy = min_y_clamped cy <= max_y_clamped cy++) {
            #             # Figure out which row of the circular buffer
            #             # we're filling in using bitmasking:
            #             uint8_t *clamped_row = clamped_storage + (cy & 7) * clamped_width
            #
            #             # Figure out which row of the input we're reading
            #             # from by clamping the y coordinate:
            #             int clamped_y = std::hl.min(std::hl.max(cy, 0), input.height()-1)
            #             uint8_t *input_row = &input(0, clamped_y)
            #
            #             # Fill it in with the padding.
            #             for (int x = -2 x < input.width() + 2 ):
            #                 int clamped_x = std::hl.min(std::hl.max(x, 0), input.width()-1)
            #                 *clamped_row++ = input_row[clamped_x]
            #
            #
            #
            #         # Now iterate over vectors of x for the pure step of the output.
            #         for (int x_vec = 0 x_vec < (input.width() + 15)/16 x_vec++) {
            #             int x_base = std::hl.min(x_vec * 16, input.width() - 16)
            #
            #             # Allocate storage for the minimum and maximum
            #             # helpers. One vector is enough.
            #             __m128i minimum_storage, maximum_storage
            #
            #             # The pure step for the maximum is a vector of zeros
            #             maximum_storage = (__m128i)_mm_setzero_ps()
            #
            #             # The update step for maximum
            #             for (int max_y = y - 2 max_y <= y + 2 max_y++) {
            #                 uint8_t *clamped_row = clamped_storage + (max_y & 7) * clamped_width
            #                 for (int max_x = x_base - 2 max_x <= x_base + 2 max_):
            #                     __m128i v = _mm_loadu_si128((__m128i const *)(clamped_row + max_x + 2))
            #                     maximum_storage = _mm_max_epu8(maximum_storage, v)
            #
            #
            #
            #             # The pure step for the minimum is a vector of
            #             # ones. Create it by comparing something to
            #             # itself.
            #             minimum_storage = (__m128i)_mm_cmpeq_ps(_mm_setzero_ps(),
            #                                                     _mm_setzero_ps())
            #
            #             # The update step for minimum.
            #             for (int min_y = y - 2 min_y <= y + 2 min_y++) {
            #                 uint8_t *clamped_row = clamped_storage + (min_y & 7) * clamped_width
            #                 for (int min_x = x_base - 2 min_x <= x_base + 2 min_):
            #                     __m128i v = _mm_loadu_si128((__m128i const *)(clamped_row + min_x + 2))
            #                     minimum_storage = _mm_min_epu8(minimum_storage, v)
            #
            #
            #
            #             # Now compute the spread.
            #             __m128i spread = _mm_sub_epi8(maximum_storage, minimum_storage)
            #
            #             # Store it.
            #             _mm_storeu_si128((__m128i *)(output_row + x_base), spread)
            #
            #
            #
            #     del clamped_storage
            #
        # end of hundred iterations

        # Skip the timing comparison if we don't have openmp
        # enabled. Otherwise it's unfair to C.
        #ifdef _OPENMP
        t2 = datetime.now()

        # Now run the Halide version again without the
        # jit-compilation overhead. Also run it one hundred times.
        for iters in range(100):
            spread.realize(halide_result)

        t3 = datetime.now()

        # Report the timings. On my machine they both take about 3ms
        # for the 4-megapixel input (fast!), which makes sense,
        # because they're using the same vectorization and
        # parallelization strategy. However I find the Halide easier
        # to read, write, debug, modify, and port.
        print("Halide spread took %f ms. C equivalent took %f ms" % (
               (t3 - t2).total_seconds() * 1000,
               (t2 - t1).total_seconds() * 1000))

        #endif # _OPENMP

        # Check the results match:
        for yy in range(input.height()):
            for xx in range(input.width()):
                if halide_result(xx, yy) != c_result(xx, yy):
                    raise Exception("halide_result(%d, %d) = %d instead of %d" % (
                           xx, yy, halide_result(xx, yy), c_result(xx, yy)))
                    return -1



        #endif # __SSE2__
    else:
        print("(Skipped the SSE2 section of the code, "
              "since non-sense in python world.)")

    print("Success!")
    return 0



if __name__ == "__main__":
    main()
