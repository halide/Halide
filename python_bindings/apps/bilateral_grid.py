"""
Bilateral histogram.
"""

from __future__ import print_function
from __future__ import division

import halide as hl

import numpy as np
from imageio import imread, imsave
import os.path

def get_bilateral_grid(input, r_sigma, s_sigma):
    x = hl.Var('x')
    y = hl.Var('y')
    z = hl.Var('z')
    c = hl.Var('c')
    xi = hl.Var("xi")
    yi = hl.Var("yi")
    zi = hl.Var("zi")

    # Add a boundary condition
    clamped = hl.BoundaryConditions.repeat_edge(input)

    # Construct the bilateral grid
    r = hl.RDom([(0, s_sigma), (0, s_sigma)], 'r')
    val = clamped[x * s_sigma + r.x - s_sigma // 2, y * s_sigma + r.y - s_sigma // 2]
    val = hl.clamp(val, 0.0, 1.0)

    zi = hl.i32(val / r_sigma + 0.5)

    histogram = hl.Func('histogram')
    histogram[x, y, z, c] = 0.0
    histogram[x, y, zi, c] += hl.select(c == 0, val, 1.0)

    # Blur the histogram using a five-tap filter
    blurx, blury, blurz = hl.Func('blurx'), hl.Func('blury'), hl.Func('blurz')
    blurz[x, y, z, c] = histogram[x, y, z-2, c] + histogram[x, y, z-1, c]*4 + histogram[x, y, z, c]*6 + histogram[x, y, z+1, c]*4 + histogram[x, y, z+2, c]
    blurx[x, y, z, c] = blurz[x-2, y, z, c] + blurz[x-1, y, z, c]*4 + blurz[x, y, z, c]*6 + blurz[x+1, y, z, c]*4 + blurz[x+2, y, z, c]
    blury[x, y, z, c] = blurx[x, y-2, z, c] + blurx[x, y-1, z, c]*4 + blurx[x, y, z, c]*6 + blurx[x, y+1, z, c]*4 + blurx[x, y+2, z, c]

    # Take trilinear samples to compute the output
    val = hl.clamp(clamped[x, y], 0.0, 1.0)
    zv = val / r_sigma
    zi = hl.i32(zv)
    zf = zv - zi
    xf = hl.f32(x % s_sigma) / s_sigma
    yf = hl.f32(y % s_sigma) / s_sigma
    xi = x / s_sigma
    yi = y / s_sigma
    interpolated = hl.Func('interpolated')
    interpolated[x, y, c] = hl.lerp(hl.lerp(hl.lerp(blury[xi, yi, zi, c], blury[xi+1, yi, zi, c], xf),
                                            hl.lerp(blury[xi, yi+1, zi, c], blury[xi+1, yi+1, zi, c], xf), yf),
                                    hl.lerp(hl.lerp(blury[xi, yi, zi+1, c], blury[xi+1, yi, zi+1, c], xf),
                                            hl.lerp(blury[xi, yi+1, zi+1, c], blury[xi+1, yi+1, zi+1, c], xf), yf), zf)

    # Normalize
    bilateral_grid = hl.Func('bilateral_grid')
    bilateral_grid[x, y] = interpolated[x, y, 0] / interpolated[x, y, 1]

    target = hl.get_target_from_environment()
    if target.has_gpu_feature():
        # GPU schedule
        # Currently running this directly from the Python code is very slow.
        # Probably because of the dispatch time because generated code
        # is same speed as C++ generated code.
        print ("Compiling for GPU.")
        histogram.compute_root().reorder(c, z, x, y).gpu_tile(x, y, 8, 8);
        histogram.update().reorder(c, r.x, r.y, x, y).gpu_tile(x, y, xi, yi, 8, 8).unroll(c)
        blurx.compute_root().gpu_tile(x, y, z, xi, yi, zi, 16, 16, 1)
        blury.compute_root().gpu_tile(x, y, z, xi, yi, zi, 16, 16, 1)
        blurz.compute_root().gpu_tile(x, y, z, xi, yi, zi, 8, 8, 4)
        bilateral_grid.compute_root().gpu_tile(x, y, xi, yi, s_sigma, s_sigma)
    else:
        # CPU schedule
        print ("Compiling for CPU.")
        histogram.compute_root().parallel(z)
        histogram.update().reorder(c, r.x, r.y, x, y).unroll(c)
        blurz.compute_root().reorder(c, z, x, y).parallel(y).vectorize(x, 4).unroll(c)
        blurx.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 4).unroll(c)
        blury.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 4).unroll(c)
        bilateral_grid.compute_root().parallel(y).vectorize(x, 4)

    return bilateral_grid


def generate_compiled_file(bilateral_grid):

    target = hl.get_target_from_environment()
    # Need to copy the filter executable from the C++ apps/bilateral_grid folder to run this.
    # (after making it of course)
    arguments = ArgumentsVector()
    arguments.append(Argument('r_sigma', InputScalar, hl.Float(32), 0))
    arguments.append(Argument('input', InputBuffer, hl.UInt(16), 2))
    bilateral_grid.compile_to_file("bilateral_grid",
                                   arguments,
                                   "bilateral_grid",
                                   target)
    print("Generated compiled file for bilateral_grid function.")


def get_input_data():
    image_path = os.path.join(os.path.dirname(__file__), "../../apps/images/rgb.png")
    assert os.path.exists(image_path), \
        "Could not find %s" % image_path
    rgb_data = imread(image_path)
    #print("rgb_data", type(rgb_data), rgb_data.shape, rgb_data.dtype)

    grey_data = np.mean(rgb_data, axis=2, dtype=np.float32)
    input_data = np.copy(grey_data, order="F") / 255.0

    return input_data

def filter_test_image(bilateral_grid, input):
    bilateral_grid.compile_jit()

    # preparing input and output memory buffers (numpy ndarrays)
    input_data = get_input_data()
    input_image = hl.Buffer(input_data)
    input.set(input_image)

    output_data = np.empty(input_data.shape, dtype=input_data.dtype, order="F")
    output_image = hl.Buffer(output_data)

    if False:
        print("input_image", input_image)
        print("output_image", output_image)

    # do the actual computation
    bilateral_grid.realize(output_image)

    # save results
    input_path = "bilateral_grid_input.png"
    output_path = "bilateral_grid.png"
    imsave(input_path, input_data)
    imsave(output_path, output_data)
    print("\nbilateral_grid realized on output_image.")
    print("Result saved at '", output_path,
          "' ( input data copy at '", input_path, "' ).", sep="")


def main():
    input = hl.ImageParam(hl.Float(32), 2, 'input')
    r_sigma = hl.Param(hl.Float(32), 'r_sigma', 0.1) # Value needed if not generating an executable
    s_sigma = 8 # This is passed during code generation in the C++ version

    bilateral_grid = get_bilateral_grid(input, r_sigma, s_sigma)

    # Set `generate` to False to run the jit immediately and get  instant gratification.
    #generate = True
    generate = False
    if generate:
        generate_compiled_file(bilateral_grid)
    else:
        filter_test_image(bilateral_grid, input)

    print("\nEnd of game. Have a nice day!")
    return

if __name__ == '__main__':
    main()
