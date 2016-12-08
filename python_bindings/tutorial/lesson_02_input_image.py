# Halide tutorial lesson 2.

# This lesson demonstrates how to pass in input images.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_02_input_image
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_02*.cpp -g -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -lpthread -ldl -o lesson_02 -std=c++11
# LD_LIBRARY_PATH=../bin ./lesson_02

# On os x:
# g++ lesson_02*.cpp -g -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -o lesson_02 -std=c++11
# DYLD_LIBRARY_PATH=../bin ./lesson_02

# The only Halide header file you need is Halide.h. It includes all of Halide.
#include "Halide.h"

import halide as h
import numpy as np
from scipy.misc import imread, imsave
import os.path

def main():

    # This program defines a single-stage imaging pipeline that
    # brightens an image.

    # First we'll load the input image we wish to brighten.
    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/rgb.png")
    input_data = imread(image_path)
    assert input_data.dtype == np.uint8

    # We create a Buffer object to wrap the numpy array
    input = h.Buffer(input_data)

    # Next we define our Func object that represents our one pipeline
    # stage.
    brighter = h.Func("brighter")

    # Our Func will have three arguments, representing the position
    # in the image and the color channel. Halide treats color
    # channels as an extra dimension of the image.
    x, y, c = h.Var("x"), h.Var("y"), h.Var("c")

    # Normally we'd probably write the whole function definition on
    # one line. Here we'll break it apart so we can explain what
    # we're doing at every step.

    # For each pixel of the input image.
    value = input[x, y, c]
    assert type(value) == h.Expr

    # Cast it to a floating point value.
    value = h.cast(h.Float(32), value)

    # Multiply it by 1.5 to brighten it. Halide represents real
    # numbers as floats, not doubles, so we stick an 'f' on the end
    # of our constant.
    value = value * 1.5

    # Clamp it to be less than 255, so we don't get overflow when we
    # cast it back to an 8-bit unsigned int.
    value = h.min(value, 255.0)

    # Cast it back to an 8-bit unsigned integer.
    value = h.cast(h.UInt(8), value)

    # Define the function.
    brighter[x, y, c] = value

    # The equivalent one-liner to all of the above is:
    #
    # brighter(x, y, c) = h.cast<uint8_t>(min(input(x, y, c) * 1.5f, 255))
    # brighter[x, y, c] = h.cast(h.UInt(8), min(input[x, y, c] * 1.5, 255))
    #
    # In the shorter version:
    # - I skipped the cast to float, because multiplying by 1.5f does
    #   that automatically.
    # - I also used integer constants in clamp, because they get cast
    #   to match the type of the first argument.
    # - I left the h. off clamp. It's unnecessary due to Koenig
    #   lookup.

    # Remember. All we've done so far is build a representation of a
    # Halide program in memory. We haven't actually processed any
    # pixels yet. We haven't even compiled that Halide program yet.

    # So now we'll realize the Func. The size of the output image
    # should match the size of the input image. If we just wanted to
    # brighten a portion of the input image we could request a
    # smaller size. If we request a larger size Halide will throw an
    # error at runtime telling us we're trying to read out of bounds
    # on the input image.
    output_image = brighter.realize(input.width(), input.height(), input.channels())
    assert type(output_image) == h.Buffer_uint8

    # Save the output for inspection. It should look like a bright parrot.
    output_data = h.buffer_to_ndarray(output_image)
    #print("output_data", output_data)
    #print("output_data.shape", output_data.shape)
    imsave("brighter.png", output_data)
    print("Created brighter.png result file.")

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
