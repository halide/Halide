#!/usr/bin/python3
# Halide tutorial lesson 4

# This lesson demonstrates how to follow what Halide is doing at runtime.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_04_debugging_2
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_04*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_04 -std=c++11
# LD_LIBRARY_PATH=../bin ./lesson_04

# On os x:
# g++ lesson_04*.cpp -g -I ../include -L ../bin -lHalide -o lesson_04 -std=c++11
# DYLD_LIBRARY_PATH=../bin ./lesson_04

#include "Halide.h"
#include <stdio.h>
#using namespace Halide
import halide as hl

def main():

    gradient = hl.Func("gradient")
    x, y = hl.Var("x"), hl.Var("y")

    # We'll define our gradient function as before.
    gradient[x, y] = x + y

    # And tell Halide that we'd like to be notified of all
    # evaluations.
    gradient.trace_stores()

    # Realize the function over an 8x8 region.
    print("Evaluating gradient")
    output = gradient.realize(8, 8)

    # This will print out all the times gradient(x, y) gets
    # evaluated.

    # Now that we can snoop on what Halide is doing, let's try our
    # first scheduling primitive. We'll make a new version of
    # gradient that processes each scanline in parallel.
    parallel_gradient = hl.Func("parallel_gradient")
    parallel_gradient[x, y] = x + y

    # We'll also trace this function.
    parallel_gradient.trace_stores()

    # Things are the same so far. We've defined the algorithm, but
    # haven't said anything about how to schedule it. In general,
    # exploring different scheduling decisions doesn't change the code
    # that describes the algorithm.

    # Now we tell Halide to use a parallel for loop over the y
    # coordinate. On linux we run this using a thread pool and a task
    # queue. On os x we call into grand central dispatch, which does
    # the same thing for us.
    parallel_gradient.parallel(y)

    # This time the printfs should come out of order, because each
    # scanline is potentially being processed in a different
    # thread. The number of threads should adapt to your system, but
    # on linux you can control it manually using the environment
    # variable HL_NUMTHREADS.
    print("\nEvaluating parallel_gradient")
    parallel_gradient.realize(8, 8)

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
