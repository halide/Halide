#!/usr/bin/python3
"""
Erode application using Python Halide bindings
"""

from halide import *

import numpy as np
from scipy.misc import imread, imsave
import os.path

def get_erode(input):
    """
    Erode on 5x5 stencil, first erode x then erode y.
    """

    x = Var("x")
    y = Var("y")
    c = Var("c")
    input_clamped = Func("input_clamped")
    erode_x = Func("erode_x")
    erode_y = Func("erode_y")

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    erode_x[x,y,c] = min(min(min(min(input_clamped[x-2,y,c],input_clamped[x-1,y,c]),input_clamped[x,y,c]),input_clamped[x+1,y,c]),input_clamped[x+2,y,c])
    erode_y[x,y,c] = min(min(min(min(erode_x[x,y-2,c],erode_x[x,y-1,c]),erode_x[x,y,c]),erode_x[x,y+1,c]),erode_x[x,y+2,c])

    yi = Var("yi")

    # CPU Schedule
    erode_x.compute_root().split(y, y, yi, 8).parallel(y)
    erode_y.compute_root().split(y, y, yi, 8).parallel(y)

    return erode_y


def get_input_data():

    image_path = os.path.join(os.path.dirname(__file__), "../../apps/images/rgb.png")
    assert os.path.exists(image_path), \
        "Could not find %s" % image_path
    rgb_data = imread(image_path)
    print("rgb_data", type(rgb_data), rgb_data.shape, rgb_data.dtype)

    input_data = np.copy(rgb_data, order="F")

    return input_data


def main():

    # define and compile the function
    input = ImageParam(UInt(8), 3, "input")
    erode = get_erode(input)
    erode.compile_jit()

    # preparing input and output memory buffers (numpy ndarrays)
    input_data = get_input_data()
    input_image = Buffer(input_data)
    input.set(input_image)

    output_data = np.empty(input_data.shape, dtype=input_data.dtype, order="F")
    output_image = Buffer(output_data)

    print("input_image", input_image)
    print("output_image", output_image)

    # do the actual computation
    erode.realize(output_image)

    # save results
    input_path = "erode_input.png"
    output_path = "erode_result.png"
    imsave(input_path, input_data)
    imsave(output_path, output_data)
    print("\nerode realized on output image.",
          "Result saved at", output_path,
          "( input data copy at", input_path, ")")

    print("\nEnd of game. Have a nice day!")
    return

if __name__ == "__main__":
    main()
