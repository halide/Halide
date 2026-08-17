#!/usr/bin/python3

# Halide tutorial lesson 23: Serialization

# This lesson describes how to serialize pipelines into a binary format
# which can be saved on disk, and later deserialized and loaded for
# evaluation.

# Note that you'll need to be using a build of Halide that was configured
# using the WITH_SERIALIZATION=ON macro defined in order for this tutorial
# to work.

# Disclaimer: Serialization is experimental in Halide 17 and is subject to
# change; we recommend that you avoid relying on it for production work at
# this time.

import os.path

import numpy as np

import halide as hl
import halide.imageio


def main():
    # First we'll declare some Vars to use below.
    x, y, c = hl.Var("x"), hl.Var("y"), hl.Var("c")

    # Let's start with the same separable blur pipeline that we used in
    # Tutorial 7, with the clamped boundary condition
    if True:
        # Let's create an ImageParam for an 8-bit RGB image that we'll use
        # for input.
        input = hl.ImageParam(hl.UInt(8), 3, "input")

        # Wrap the input in a Func that prevents reading out of bounds:
        clamped = hl.Func("clamped")
        clamped_x = hl.clamp(x, 0, input.width() - 1)
        clamped_y = hl.clamp(y, 0, input.height() - 1)
        clamped[x, y, c] = input[clamped_x, clamped_y, c]

        # Upgrade it to 16-bit, so we can do math without it overflowing.
        input_16 = hl.Func("input_16")
        input_16[x, y, c] = hl.u16(clamped[x, y, c])

        # Blur it horizontally:
        blur_x = hl.Func("blur_x")
        blur_x[x, y, c] = (
            input_16[x - 1, y, c] + 2 * input_16[x, y, c] + input_16[x + 1, y, c]
        ) // 4

        # Blur it vertically:
        blur_y = hl.Func("blur_y")
        blur_y[x, y, c] = (
            blur_x[x, y - 1, c] + 2 * blur_x[x, y, c] + blur_x[x, y + 1, c]
        ) // 4

        # Convert back to 8-bit.
        output = hl.Func("output")
        output[x, y, c] = hl.u8(blur_y[x, y, c])

        # Now lets serialize the pipeline to disk (must use the .hlpipe file
        # extension)
        blur_pipeline = hl.Pipeline(output)
        params = hl.serialize_pipeline(blur_pipeline, "blur.hlpipe", get_params=True)

        # The call to serialize_pipeline populates the params map with any
        # input or output parameters that were found ... objects we'll need
        # to attach to buffers if we wish to execute the pipeline
        for name in params:
            print(f"Found Param: {name}")

    # new scope ... everything above is now unreachable! Now lets
    # reconstruct the entire pipeline from scratch by deserializing it from
    # a file
    if True:
        # Lets load a color 8-bit input and connect it to an ImageParam
        image_path = os.path.join(
            os.path.dirname(__file__), "../../tutorial/images/rgb.png"
        )
        rgb_image = hl.Buffer(halide.imageio.imread(image_path))
        input = hl.ImageParam(hl.UInt(8), 3, "input")
        input.set(rgb_image)

        # Now lets populate the params map so we can override the input
        params = {"input": input.parameter()}

        # Lets construct a new pipeline from scratch by deserializing the
        # file we wrote to disk
        blur_pipeline = hl.deserialize_pipeline("blur.hlpipe", params)

        # Now realize the pipeline and blur out input image
        result = blur_pipeline.realize([rgb_image.width(), rgb_image.height(), 3])

        # Now lets save the result ... we should have another blurry parrot!
        # python3-imageio versions <2.5 expect a numpy array
        halide.imageio.imwrite("another_blurry_parrot.png", np.asanyarray(result))

    # new scope ... everything above is now unreachable!
    if True:
        # Lets do the same thing again ... construct a new pipeline from
        # scratch by deserializing the file we wrote to disk

        # First we can deserialize the external parameters (useful in the
        # event we want to remap them and replace the definitions with our
        # own user parameter definitions)
        params = hl.deserialize_parameters("blur.hlpipe")

        # Now deserialize the pipeline from file
        blur_pipeline = hl.deserialize_pipeline("blur.hlpipe", params)

        # Now, lets serialize it to an in memory buffer ... rather than
        # writing it to disk
        data, params = hl.serialize_pipeline(blur_pipeline, get_params=True)

        # Now lets deserialize it from memory
        hl.deserialize_pipeline(data, params)

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
