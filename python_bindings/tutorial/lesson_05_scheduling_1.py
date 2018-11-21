#!/usr/bin/python3
# Halide tutorial lesson 5

# This lesson demonstrates how to manipulate the order in which you
# evaluate pixels in a hl.Func, including vectorization,
# parallelization, unrolling, and tiling.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_05_scheduling_1
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_05*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_05 -std=c++11
# LD_LIBRARY_PATH=../bin ./lesson_05

# On os x:
# g++ lesson_05*.cpp -g -I ../include -L ../bin -lHalide -o lesson_05 -std=c++11
# DYLD_LIBRARY_PATH=../bin ./lesson_05

#include "Halide.h"
#include <stdio.h>
#using namespace Halide
import halide as hl

def main():

    # We're going to define and schedule our gradient function in
    # several different ways, and see what order pixels are computed
    # in.

    x, y = hl.Var("x"), hl.Var ("y")

    # First we observe the default ordering.
    if True:
        gradient = hl.Func("gradient")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # By default we walk along the rows and then down the
        # columns. This means x varies quickly, and y varies
        # slowly. x is the column and y is the row, so this is a
        # row-major traversal.
        print("Evaluating gradient row-major")
        output = gradient.realize(4, 4)

        # The equivalent C is:
        print("Equivalent C:")
        for yy in range(4):
            for xx in range(4):
                print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))


        print("\n")

        # Tracing is one useful way to understand what a schedule is
        # doing. You can also ask Halide to print out pseudocode
        # showing what loops Halide is generating:
        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()

        # Because we're using the default ordering, it should print:
        # compute gradient:
        #   for y:
        #     for x:
        #       gradient(...) = ...


    # Reorder variables.
    if True:
        gradient = hl.Func("gradient_col_major")
        print("x, y", x, y)
        gradient[x, y] = x + y
        gradient.trace_stores()

        # If we reorder x and y, we can walk down the columns
        # instead. The reorder call takes the arguments of the func,
        # and sets a new nesting order for the for loops that are
        # generated. The arguments are specified from the innermost
        # loop out, so the following call puts y in the inner loop:
        gradient.reorder(y, x)
        #gradient.reorder((y, x))

        # This means y (the row) will vary quickly, and x (the
        # column) will vary slowly, so this is a column-major
        # traversal.

        print("Evaluating gradient column-major")
        output = gradient.realize(4, 4)

        print("Equivalent C:")
        for yy in range(4):
            for xx in range(4):
                print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        # If we print pseudo-code for this schedule, we'll see that
        # the loop over y is now inside the loop over x.
        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()


    # Split a variable into two.
    if True:
        gradient = hl.Func("gradient_split")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # The most powerful primitive scheduling operation you can do
        # to a var is to split it into inner and outer sub-variables:
        x_outer, x_inner = hl.Var("x_outer"), hl.Var("x_inner")
        gradient.split(x, x_outer, x_inner, 2)

        # This breaks the loop over x into two nested loops: an outer
        # one over x_outer, and an inner one over x_inner. The last
        # argument to split was the "split factor". The inner loop
        # runs from zero to the split factor. The outer loop runs
        # from zero to the extent required of x (4 in this case)
        # divided by the split factor. Within the loops, the old
        # variable is defined to be outer * factor + inner. If the
        # old loop started at a value other than zero, then that is
        # also added within the loops.

        print("Evaluating gradient with x split into x_outer and x_inner ")
        output = gradient.realize(4, 4)

        print("Equivalent C:")
        for yy in range(4):
            for x_outer in range(2):
                for x_inner in range(2):
                    xx = x_outer * 2 + x_inner
                    print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()

        # Note that the order of evaluation of pixels didn't actually
        # change! Splitting by itself does nothing, but it does open
        # up all of the scheduling possibilities that we will explore
        # below.


    # Fuse two variables into one.
    if True:
        gradient = hl.Func("gradient_fused")
        gradient[x, y] = x + y

        # The opposite of splitting is 'fusing'. Fusing two variables
        # merges the two loops into a single for loop over the
        # product of the extents. Fusing is less important than
        # splitting, but it also sees use (as we'll see later in this
        # lesson). Like splitting, fusing by itself doesn't change
        # the order of evaluation.
        fused = hl.Var("fused")
        gradient.fuse(x, y, fused)

        print("Evaluating gradient with x and y fused")
        output = gradient.realize(4, 4)

        print("Equivalent C:")
        for fused in range(4*4):
            yy = fused / 4
            xx = fused % 4
            print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()


    # Evaluating in tiles.
    if True:
        gradient = hl.Func("gradient_tiled")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # Now that we can both split and reorder, we can do tiled
        # evaluation. Let's split both x and y by a factor of two,
        # and then reorder the vars to express a tiled traversal.
        #
        # A tiled traversal splits the domain into small rectangular
        # tiles, and outermost iterates over the tiles, and within
        # that iterates over the points within each tile. It can be
        # good for performance if neighboring pixels use overlapping
        # input data, for example in a blur. We can express a tiled
        # traversal like so:
        x_outer, x_inner, y_outer, y_inner = hl.Var(), hl.Var(), hl.Var(), hl.Var()
        gradient.split(x, x_outer, x_inner, 2)
        gradient.split(y, y_outer, y_inner, 2)
        gradient.reorder(x_inner, y_inner, x_outer, y_outer)

        # This pattern is common enough that there's a shorthand for it:
        # gradient.tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2)

        print("Evaluating gradient in 2x2 tiles")
        output = gradient.realize(4, 4)

        print("Equivalent C:")
        for y_outer in range(2):
            for x_outer in range(2):
                for y_inner in range(2):
                    for x_inner in range(2):
                        xx = x_outer * 2 + x_inner
                        yy = y_outer * 2 + y_inner
                        print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()


    # Evaluating in vectors.
    if True:
        gradient = hl.Func("gradient_in_vectors")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # The nice thing about splitting is that it guarantees the
        # inner variable runs from zero to the split factor. Most of
        # the time the split-factor will be a compile-time constant,
        # so we can replace the loop over the inner variable with a
        # single vectorized computation. This time we'll split by a
        # factor of four, because on X86 we can use SSE to compute in
        # 4-wide vectors.
        x_outer, x_inner = hl.Var("x_outer"), hl.Var("x_inner")
        gradient.split(x, x_outer, x_inner, 4)
        gradient.vectorize(x_inner)

        # Splitting and then vectorizing the inner variable is common
        # enough that there's a short-hand for it. We could have also
        # said:
        #
        # gradient.vectorize(x, 4)
        #
        # which is equivalent to:
        #
        # gradient.split(x, x, x_inner, 4)
        # gradient.vectorize(x_inner)
        #
        # Note that in this case we reused the name 'x' as the new
        # outer variable. Later scheduling calls that refer to x
        # will refer to this new outer variable named x.

        # This time we'll evaluate over an 8x4 box, so that we have
        # more than one vector of work per scanline.
        print("Evaluating gradient with x_inner vectorized ")
        output = gradient.realize(8, 4)

        print("Equivalent C:")
        for yy in range(4):
            for x_outer in range(2):
                # The loop over x_inner has gone away, and has been
                # replaced by a vectorized version of the
                # expression. On x86 processors, Halide generates SSE
                # for all of this.
                x_vec = [x_outer * 4 + 0,
                               x_outer * 4 + 1,
                               x_outer * 4 + 2,
                               x_outer * 4 + 3]
                val = [x_vec[0] + yy,
                             x_vec[1] + yy,
                             x_vec[2] + yy,
                             x_vec[3] + yy]
                print("Evaluating at <%d, %d, %d, %d>, <%d, %d, %d, %d>: <%d, %d, %d, %d>" % (
                       x_vec[0], x_vec[1], x_vec[2], x_vec[3],
                       yy, yy, yy, yy,
                       val[0], val[1], val[2], val[3]))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()


    # Unrolling a loop.
    if True:
        gradient = hl.Func("gradient_in_vectors")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # If multiple pixels share overlapping data, it can make
        # sense to unroll a computation so that shared values are
        # only computed or loaded once. We do this similarly to how
        # we expressed vectorizing. We split a dimension and then
        # fully unroll the loop of the inner variable. Unrolling
        # doesn't change the order in which things are evaluated.
        x_outer, x_inner = hl.Var("x_outer"), hl.Var("x_inner")
        gradient.split(x, x_outer, x_inner, 2)
        gradient.unroll(x_inner)

        # The shorthand for this is:
        # gradient.unroll(x, 2)

        print("Evaluating gradient unrolled by a factor of two")
        result = gradient.realize(4, 4)


        print("Equivalent C:")
        for yy in range(4):
            for x_outer in range(2):
                # Instead of a for loop over x_inner, we get two
                # copies of the innermost statement.
                if True:
                    x_inner = 0
                    xx = x_outer * 2 + x_inner
                    print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

                if True:
                    x_inner = 1
                    xx = x_outer * 2 + x_inner
                    print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()


    # Splitting by factors that don't divide the extent.
    if True:
        gradient = hl.Func("gradient_split_5x4")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # Splitting guarantees that the inner loop runs from zero to
        # the split factor, which is important for the uses we saw
        # above. So what happens when the total extent we wish to
        # evaluate x over isn't a multiple of the split factor? We'll
        # split by a factor of two again, but now we'll evaluate
        # gradient over a 5x4 box instead of the 4x4 box we've been
        # using.
        x_outer, x_inner = hl.Var("x_outer"), hl.Var("x_inner")
        gradient.split(x, x_outer, x_inner, 2)

        print("Evaluating gradient over a 5x4 box with x split by two ")
        output = gradient.realize(5, 4)

        print("Equivalent C:")
        for yy in range(4):
            for x_outer in range(3): # Now runs from 0 to 3
                for x_inner in range(2):
                    xx = x_outer * 2
                    # Before we add x_inner, make sure we don't
                    # evaluate points outside of the 5x4 box. We'll
                    # hl.clamp x to be at most 3 (5 minus the split
                    # factor).
                    if xx > 3:
                        xx = 3
                    xx += x_inner
                    print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()

        # If you read the output, you'll see that some coordinates
        # were evaluated more than once! That's generally OK, because
        # pure Halide functions have no side-effects, so it's safe to
        # evaluate the same point multiple times. If you're calling
        # out to C functions like we are, it's your responsibility to
        # make sure you can handle the same point being evaluated
        # multiple times.

        # The general rule is: If we require x from x_min to x_min + x_extent, and
        # we split by a factor 'factor', then:
        #
        # x_outer runs from 0 to (x_extent + factor - 1)/factor
        # x_inner runs from 0 to factor
        # x = hl.min(x_outer * factor, x_extent - factor) + x_inner + x_min
        #
        # In our example, x_min was 0, x_extent was 5, and factor was 2.

        # However, if you write a Halide function with an update
        # definition (see lesson 9), then it is not safe to evaluate
        # the same point multiple times, so we won't apply this
        # trick. Instead the range of values computed will be rounded
        # up to the next multiple of the split factor.


    # Fusing, tiling, and parallelizing.
    if True:
        # We saw in the previous lesson that we can parallelize
        # across a variable. Here we combine it with fusing and
        # tiling to express a useful pattern - processing tiles in
        # parallel.

        # This is where fusing shines. Fusing helps when you want to
        # parallelize across multiple dimensions without introducing
        # nested parallelism. Nested parallelism (parallel for loops
        # within parallel for loops) is supported by Halide, but
        # often gives poor performance compared to fusing the
        # parallel variables into a single parallel for loop.

        gradient = hl.Func("gradient_fused_tiles")
        gradient[x, y] = x + y
        gradient.trace_stores()

        # First we'll tile, then we'll fuse the tile indices and
        # parallelize across the combination.
        x_outer, y_outer = hl.Var("x_outer"), hl.Var("y_outer")
        x_inner, y_inner = hl.Var("x_inner"), hl.Var("y_inner")
        tile_index = hl.Var("tile_index")
        gradient.tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2)
        gradient.fuse(x_outer, y_outer, tile_index)
        gradient.parallel(tile_index)

        # The scheduling calls all return a reference to the hl.Func, so
        # you can also chain them together into a single statement to
        # make things slightly clearer:
        #
        # gradient
        #     .tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2)
        #     .fuse(x_outer, y_outer, tile_index)
        #     .parallel(tile_index)


        print("Evaluating gradient tiles in parallel")
        output = gradient.realize(4, 4)

        # The tiles should occur in arbitrary order, but within each
        # tile the pixels will be traversed in row-major order.

        print("Equivalent (serial) C:")
        # This outermost loop should be a parallel for loop, but that's hard in C.
        for tile_index in range(4):
            y_outer = tile_index / 2
            x_outer = tile_index % 2
            for y_inner in range(2):
                for x_inner in range(2):
                    yy = y_outer * 2 + y_inner
                    xx = x_outer * 2 + x_inner
                    print("Evaluating at x = %d, y = %d: %d" % (xx, yy, xx + yy))

        print()

        print("Pseudo-code for the schedule:")
        gradient.print_loop_nest()
        print()


    # Putting it all together.
    if True:
        # Are you ready? We're going to use all of the features above now.
        gradient_fast = hl.Func("gradient_fast")
        gradient_fast[x, y] = x + y

        # We'll process 256x256 tiles in parallel.
        x_outer, y_outer = hl.Var("x_outer"), hl.Var("y_outer")
        x_inner, y_inner = hl.Var("x_inner"), hl.Var("y_inner")
        tile_index = hl.Var("tile_index")
        gradient_fast \
            .tile(x, y, x_outer, y_outer, x_inner, y_inner, 256, 256) \
            .fuse(x_outer, y_outer, tile_index) \
            .parallel(tile_index)

        # We'll compute two scanlines at once while we walk across
        # each tile. We'll also vectorize in x. The easiest way to
        # express this is to recursively tile again within each tile
        # into 4x2 subtiles, then vectorize the subtiles across x and
        # unroll them across y:
        x_inner_outer, y_inner_outer = hl.Var("x_inner_outer"), hl.Var("y_inner_outer")
        x_vectors, y_pairs = hl.Var("x_vectors"), hl.Var("y_pairs")
        gradient_fast \
            .tile(x_inner, y_inner, x_inner_outer, y_inner_outer, x_vectors, y_pairs, 4, 2) \
            .vectorize(x_vectors) \
            .unroll(y_pairs)

        # Note that we didn't do any explicit splitting or
        # reordering. Those are the most important primitive
        # operations, but mostly they are buried underneath tiling,
        # vectorizing, or unrolling calls.

        # Now let's evaluate this over a range which is not a
        # multiple of the tile size.

        # If you like you can turn on tracing, but it's going to
        # produce a lot of prints. Instead we'll compute the answer
        # both in C and Halide and see if the answers match.
        result = gradient_fast.realize(800, 600)

        print("Checking Halide result against equivalent C...")
        for tile_index in range(4*3):
            y_outer = tile_index // 4
            x_outer = tile_index % 4
            for y_inner_outer in range(256//2):
                for x_inner_outer in range(256//4):
                    # We're vectorized across x
                    xx = min(x_outer * 256, 800-256) + x_inner_outer*4
                    x_vec = [xx + 0,
                                    xx + 1,
                                    xx + 2,
                                    xx + 3]

                    # And we unrolled across y
                    y_base = min(y_outer * 256, 600-256) + y_inner_outer*2

                    if True:
                        # y_pairs = 0
                        yy = y_base + 0
                        y_vec = [yy, yy, yy, yy]
                        val = [x_vec[0] + y_vec[0],
                                      x_vec[1] + y_vec[1],
                                      x_vec[2] + y_vec[2],
                                      x_vec[3] + y_vec[3]]

                        # Check the result.
                        for i in range(4):
                            #print("x_vec[%i], y_vec[%i]" % (i, i),
                            #      x_vec[i], y_vec[i])
                            if result[x_vec[i], y_vec[i]] != val[i]:
                                print("There was an error at %d %d!" % (x_vec[i], y_vec[i]))
                                return -1



                    if True:
                        # y_pairs = 1
                        yy = y_base + 1
                        y_vec = [yy, yy, yy, yy]
                        val = [x_vec[0] + y_vec[0],
                                      x_vec[1] + y_vec[1],
                                      x_vec[2] + y_vec[2],
                                      x_vec[3] + y_vec[3]]

                        # Check the result.
                        for i in range(4):
                            if result[x_vec[i], y_vec[i]] != val[i]:
                                print("There was an error at %d %d!" % (x_vec[i], y_vec[i]))


        print()

        print("Pseudo-code for the schedule:")
        gradient_fast.print_loop_nest()
        print()

        # Note that in the Halide version, the algorithm is specified
        # once at the top, separately from the optimizations, and there
        # aren't that many lines of code total. Compare this to the C
        # version. There's more code (and it isn't even parallelized or
        # vectorized properly). More annoyingly, the statement of the
        # algorithm (the result is x plus y) is buried in multiple places
        # within the mess. This C code is hard to write, hard to read,
        # hard to debug, and hard to optimize further. This is why Halide
        # exists.

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
