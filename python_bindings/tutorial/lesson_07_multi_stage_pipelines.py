#!/usr/bin/python3
# Halide tutorial lesson 7

# This lesson demonstrates how express multi-stage pipelines.

# This lesson can be built by invoking the command:
# make tutorial_lesson_07_multi_stage_pipelines
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_07*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -lpthread -ldl -o lesson_07
# LD_LIBRARY_PATH=../bin ./lesson_07

# On os x:
# g++ lesson_07*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -o lesson_07
# DYLD_LIBRARY_PATH=../bin ./lesson_07

#include "Halide.h"
#include <stdio.h>

#using namespace Halide
from halide import *

# Support code for loading pngs.
#include "image_io.h"
from scipy.misc import imread, imsave

import os.path

def main():
    # First we'll declare some Vars to use below.
    x, y, c = Var("x"), Var("y"), Var("c")

    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/rgb.png")

    # Now we'll express a multi-stage pipeline that blurs an image
    # first horizontally, and then vertically.
    if True:
        # Take a color 8-bit input
        input = Buffer(imread(image_path))
        assert input.type() == UInt(8)

        # Upgrade it to 16-bit, so we can do math without it overflowing.
        input_16 = Func("input_16")
        input_16[x, y, c] = cast(UInt(16), input[x, y, c])

        # Blur it horizontally:
        blur_x = Func("blur_x")
        blur_x[x, y, c] = (input_16[x - 1, y, c] + 2 * input_16[x, y, c] + input_16[x + 1, y, c]) / 4

        # Blur it vertically:
        blur_y = Func("blur_y")
        blur_y[x, y, c] = (blur_x[x, y - 1, c] + 2 * blur_x[x, y, c] + blur_x[x, y + 1, c]) / 4

        # Convert back to 8-bit.
        output = Func("output")
        output[x, y, c] = cast(UInt(8), blur_y[x, y, c])

        # Each Func in this pipeline calls a previous one using
        # familiar function call syntax (we've overloaded operator()
        # on Func objects). A Func may call any other Func that has
        # been given a definition. This restriction prevents
        # pipelines with loops in them. Halide pipelines are always
        # feed-forward graphs of Funcs.

        # Now let's realize it...

        # result = output.realize(input.width(), input.height(), 3)

        # Except that the line above is not going to work. Uncomment
        # it to see what happens.

        # Realizing this pipeline over the same domain as the input
        # image requires reading pixels out of bounds in the input,
        # because the blur_x stage reaches outwards horizontally, and
        # the blur_y stage reaches outwards vertically. Halide
        # detects this by injecting a piece of code at the top of the
        # pipeline that computes the region over which the input will
        # be read. When it starts to run the pipeline it first runs
        # this code, determines that the input will be read out of
        # bounds, and refuses to continue. No actual bounds checks
        # occur in the inner loop that would be slow.
        #
        # So what do we do? There are a few options. If we realize
        # over a domain shifted inwards by one pixel, we won't be
        # asking the Halide routine to read out of bounds. We saw how
        # to do this in the previous lesson:
        result = Buffer(UInt(8), input.width() - 2, input.height() - 2, 3)
        result.set_min(1, 1)
        output.realize(result)

        # Save the result. It should look like a slightly blurry
        # parrot, and it should be two pixels narrower and two pixels
        # shorter than the input image.

        result_data = buffer_to_ndarray(result)
        print("result.shape", result_data.shape)

        imsave("blurry_parrot_1.png", result_data)
        print("Created blurry_parrot_1.png")

        # This is usually the fastest way to deal with boundaries:
        # don't write code that reads out of bounds :) The more
        # general solution is our next example.


    # The same pipeline, with a boundary condition on the input.
    if True:
        # Take a color 8-bit input
        input = Buffer(imread(image_path))
        assert input.type() == UInt(8)

        # This time, we'll wrap the input in a Func that prevents
        # reading out of bounds:
        clamped = Func("clamped")

        # Define an expression that clamps x to lie within the the
        # range [0, input.width()-1].
        clamped_x = clamp(x, 0, input.width() - 1)
        # Similarly clamp y.
        clamped_y = clamp(y, 0, input.height() - 1)
        # Load from input at the clamped coordinates. This means that
        # no matter how we evaluated the Func 'clamped', we'll never
        # read out of bounds on the input. This is a clamp-to-edge
        # style boundary condition, and is the simplest boundary
        # condition to express in Halide.
        clamped[x, y, c] = input[clamped_x, clamped_y, c]

        # Defining 'clamped' in that way can be done more concisely
        # using a helper function from the BoundaryConditions
        # namespace like so:
        #
        # clamped = BoundaryConditions::repeat_edge(input)
        #
        # These are important to use for other boundary conditions,
        # because they are expressed in the way that Halide can best
        # understand and optimize.

        # Upgrade it to 16-bit, so we can do math without it
        # overflowing. This time we'll refer to our new Func
        # 'clamped', instead of referring to the input image
        # directly.
        input_16 = Func("input_16")
        input_16[x, y, c] = cast(UInt(16), clamped[x, y, c])

        # The rest of the pipeline will be the same...

        # Blur it horizontally:
        blur_x = Func("blur_x")
        blur_x[x, y, c] = (input_16[x - 1, y, c] + 2 * input_16[x, y, c] + input_16[x + 1, y, c]) / 4

        # Blur it vertically:
        blur_y = Func("blur_y")
        blur_y[x, y, c] = (blur_x[x, y - 1, c] + 2 * blur_x[x, y, c] + blur_x[x, y + 1, c]) / 4

        # Convert back to 8-bit.
        output = Func("output")
        output[x, y, c] = cast(UInt(8), blur_y[x, y, c])

        # This time it's safe to evaluate the output over the some
        # domain as the input, because we have a boundary condition.
        result = output.realize(input.width(), input.height(), 3)

        # Save the result. It should look like a slightly blurry
        # parrot, but this time it will be the same size as the
        # input.
        result_data = buffer_to_ndarray(result)
        print("result.shape", result_data.shape)

        imsave("blurry_parrot_2.png", result_data)
        print("Created blurry_parrot_2.png")

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
