import halide as hl

import numpy as np
from imageio import imread, imsave
import os.path

def get_blur(input):
    assert type(input) == hl.ImageParam
    assert input.dimensions() == 2

    x, y = hl.Var("x"), hl.Var("y")

    clamped_input = hl.BoundaryConditions.repeat_edge(input)

    input_uint16 = hl.Func("input_uint16")
    input_uint16[x,y] = hl.u16(clamped_input[x,y])
    ci = input_uint16

    blur_x = hl.Func("blur_x")
    blur_y = hl.Func("blur_y")

    blur_x[x,y] = (ci[x,y]+ci[x+1,y]+ci[x+2,y])/3
    blur_y[x,y] = hl.cast(hl.UInt(8), (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3)

    # schedule
    xi, yi = hl.Var("xi"), hl.Var("yi")
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)

    return blur_y


def get_input_data():
    image_path = os.path.join(os.path.dirname(__file__), "../../apps/images/rgb.png")
    assert os.path.exists(image_path), \
        "Could not find %s" % image_path
    rgb_data = imread(image_path)
    print("rgb_data", type(rgb_data), rgb_data.shape, rgb_data.dtype)

    grey_data = np.mean(rgb_data, axis=2, dtype=np.float32).astype(rgb_data.dtype)
    input_data = np.copy(grey_data, order="F")

    return input_data

def main():
    # define and compile the function
    input = hl.ImageParam(hl.UInt(8), 2, "input_param")
    blur = get_blur(input)
    blur.compile_jit()

    # preparing input and output memory buffers (numpy ndarrays)
    input_data = get_input_data()
    input_image = hl.Buffer(input_data)
    input.set(input_image)

    output_data = np.empty(input_data.shape, dtype=input_data.dtype, order="F")
    output_image = hl.Buffer(output_data)

    print("input_image", input_image)
    print("output_image", output_image)

    # do the actual computation
    blur.realize(output_image)

    # save results
    input_path = "blur_input.png"
    output_path = "blur_result.png"
    imsave(input_path, input_data)
    imsave(output_path, output_data)
    print("\nblur realized on output image.",
          "Result saved at", output_path,
          "( input data copy at", input_path, ")")

    print("\nEnd of game. Have a nice day!")
    return


if __name__ == "__main__":
    main()
