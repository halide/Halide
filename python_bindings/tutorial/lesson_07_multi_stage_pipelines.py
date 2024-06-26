#!/usr/bin/python3

# Halide tutorial lesson 7

# This lesson demonstrates how express multi-stage pipelines.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_07_multi_stage_pipelines
# in a shell with the current directory at python_bindings/

import halide as hl

import halide.imageio
import numpy as np
import os.path


def main():
    # First we'll declare some Vars to use below.
    x, y, c = hl.Var("x"), hl.Var("y"), hl.Var("c")

    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/rgb.png")

    # Now we'll express a multi-stage pipeline that blurs an image
    # first horizontally, and then vertically.
    if True:
        # Take a color 8-bit input
        input = hl.Buffer(halide.imageio.imread(image_path))
        assert input.type() == hl.UInt(8)

        # Upgrade it to 16-bit, so we can do math without it overflowing.
        input_16 = hl.Func("input_16")
        input_16[x, y, c] = hl.cast(hl.UInt(16), input[x, y, c])

        # Blur it horizontally:
        blur_x = hl.Func("blur_x")
        blur_x[x, y, c] = (input_16[x - 1, y, c] + 2 *
                           input_16[x, y, c] + input_16[x + 1, y, c]) / 4

        # Blur it vertically:
        blur_y = hl.Func("blur_y")
        blur_y[x, y, c] = (blur_x[x, y - 1, c] + 2 *
                           blur_x[x, y, c] + blur_x[x, y + 1, c]) / 4

        # Convert back to 8-bit.
        output = hl.Func("output")
        output[x, y, c] = hl.cast(hl.UInt(8), blur_y[x, y, c])

        # Each hl.Func in this pipeline calls a previous one using
        # familiar function call syntax (we've overloaded operator()
        # on hl.Func objects). A hl.Func may call any other hl.Func that has
        # been given a definition. This restriction prevents
        # pipelines with loops in them. Halide pipelines are always
        # feed-forward graphs of Funcs.

        # Now let's realize it...

        # result = output.realize([input.width(), input.height(), 3])

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
        result = hl.Buffer(hl.UInt(8), [input.width() - 2, input.height() - 2, 3])
        result.set_min([1, 1])
        output.realize(result)

        # Save the result. It should look like a slightly blurry
        # parrot, and it should be two pixels narrower and two pixels
        # shorter than the input image.

        # python3-imageio versions <2.5 expect a numpy array
        halide.imageio.imwrite("blurry_parrot_1.png", np.asanyarray(result))
        print("Created blurry_parrot_1.png")

        # This is usually the fastest way to deal with boundaries:
        # don't write code that reads out of bounds :) The more
        # general solution is our next example.

    # The same pipeline, with a boundary condition on the input.
    if True:
        # Take a color 8-bit input
        input = hl.Buffer(halide.imageio.imread(image_path))
        assert input.type() == hl.UInt(8)

        # This time, we'll wrap the input in a hl.Func that prevents
        # reading out of bounds:
        clamped = hl.Func("clamped")

        # Define an expression that clamps x to lie within the the
        # range [0, input.width()-1].
        clamped_x = hl.clamp(x, 0, input.width() - 1)
        # Similarly hl.clamp y.
        clamped_y = hl.clamp(y, 0, input.height() - 1)
        # Load from input at the clamped coordinates. This means that
        # no matter how we evaluated the hl.Func 'clamped', we'll never
        # read out of bounds on the input. This is a hl.clamp-to-edge
        # style boundary condition, and is the simplest boundary
        # condition to express in Halide.
        clamped[x, y, c] = input[clamped_x, clamped_y, c]

        # Defining 'clamped' in that way can be done more concisely
        # using a helper function from the BoundaryConditions
        # namespace like so:
        #
        # clamped = hl.BoundaryConditions.repeat_edge(input)
        #
        # These are important to use for other boundary conditions,
        # because they are expressed in the way that Halide can best
        # understand and optimize.

        # Upgrade it to 16-bit, so we can do math without it
        # overflowing. This time we'll refer to our new hl.Func
        # 'clamped', instead of referring to the input image
        # directly.
        input_16 = hl.Func("input_16")
        input_16[x, y, c] = hl.cast(hl.UInt(16), clamped[x, y, c])

        # The rest of the pipeline will be the same...

        # Blur it horizontally:
        blur_x = hl.Func("blur_x")
        blur_x[x, y, c] = (input_16[x - 1, y, c] + 2 *
                           input_16[x, y, c] + input_16[x + 1, y, c]) / 4

        # Blur it vertically:
        blur_y = hl.Func("blur_y")
        blur_y[x, y, c] = (blur_x[x, y - 1, c] + 2 *
                           blur_x[x, y, c] + blur_x[x, y + 1, c]) / 4

        # Convert back to 8-bit.
        output = hl.Func("output")
        output[x, y, c] = hl.cast(hl.UInt(8), blur_y[x, y, c])

        # This time it's safe to evaluate the output over the some
        # domain as the input, because we have a boundary condition.
        result = output.realize([input.width(), input.height(), 3])

        # Save the result. It should look like a slightly blurry
        # parrot, but this time it will be the same size as the
        # input.

        # python3-imageio versions <2.5 expect a numpy array
        halide.imageio.imwrite("blurry_parrot_2.png", np.asanyarray(result))
        print("Created blurry_parrot_2.png")

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
