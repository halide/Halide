"""
Local Laplacian, see e.g. Aubry et al 2011, "Fast and Robust Pyramid-based Image Processing".
"""

from __future__ import print_function
from __future__ import division

import halide as hl

import numpy as np
from imageio import imread, imsave
import os.path


int_t = hl.Int(32)
float_t = hl.Float(32)

def get_local_laplacian(input, levels, alpha, beta, J=8):
    downsample_counter=[0]
    upsample_counter=[0]

    x = hl.Var('x')
    y = hl.Var('y')

    def downsample(f):
        downx, downy = hl.Func('downx%d'%downsample_counter[0]), hl.Func('downy%d'%downsample_counter[0])
        downsample_counter[0] += 1

        downx[x,y,c] = (f[2*x-1,y,c] + 3.0*(f[2*x,y,c]+f[2*x+1,y,c]) + f[2*x+2,y,c])/8.0
        downy[x,y,c] = (downx[x,2*y-1,c] + 3.0*(downx[x,2*y,c]+downx[x,2*y+1,c]) + downx[x,2*y+2,c])/8.0

        return downy

    def upsample(f):
        upx, upy = hl.Func('upx%d'%upsample_counter[0]), hl.Func('upy%d'%upsample_counter[0])
        upsample_counter[0] += 1

        upx[x,y,c] = 0.25 * f[(x//2) - 1 + 2*(x%2),y,c] + 0.75 * f[x//2,y,c]
        upy[x,y,c] = 0.25 * upx[x, (y//2) - 1 + 2*(y%2),c] + 0.75 * upx[x,y//2,c]

        return upy

    def downsample2D(f):
        downx, downy = hl.Func('downx%d'%downsample_counter[0]), hl.Func('downy%d'%downsample_counter[0])
        downsample_counter[0] += 1

        downx[x,y] = (f[2*x-1,y] + 3.0*(f[2*x,y]+f[2*x+1,y]) + f[2*x+2,y])/8.0
        downy[x,y] = (downx[x,2*y-1] + 3.0*(downx[x,2*y]+downx[x,2*y+1]) + downx[x,2*y+2])/8.0

        return downy

    def upsample2D(f):
        upx, upy = hl.Func('upx%d'%upsample_counter[0]), hl.Func('upy%d'%upsample_counter[0])
        upsample_counter[0] += 1

        upx[x,y] = 0.25 * f[(x//2) - 1 + 2*(x%2),y] + 0.75 * f[x//2,y]
        upy[x,y] = 0.25 * upx[x, (y//2) - 1 + 2*(y%2)] + 0.75 * upx[x,y//2]

        return upy

    # THE ALGORITHM

    # loop variables
    c = hl.Var('c')
    k = hl.Var('k')

    # Make the remapping function as a lookup table.
    remap = hl.Func('remap')
    fx = hl.cast(float_t, x/256.0)
    #remap[x] = alpha*fx*exp(-fx*fx/2.0)
    remap[x] = alpha*fx*hl.exp(-fx*fx/2.0)

    # Convert to floating point
    floating = hl.Func('floating')
    floating[x,y,c] = hl.cast(float_t, input[x,y,c]) / 65535.0

    # Set a boundary condition
    clamped = hl.Func('clamped')
    clamped[x,y,c] = floating[hl.clamp(x, 0, input.width()-1), hl.clamp(y, 0, input.height()-1), c]

    # Get the luminance channel
    gray = hl.Func('gray')
    gray[x,y] = 0.299*clamped[x,y,0] + 0.587*clamped[x,y,1] + 0.114*clamped[x,y,2]

    # Make the processed Gaussian pyramid.
    gPyramid = [hl.Func('gPyramid%d'%i) for i in range(J)]
    # Do a lookup into a lut with 256 entires per intensity level
    level = k / (levels - 1)
    idx = gray[x,y]*hl.cast(float_t, levels-1)*256.0
    idx = hl.clamp(hl.cast(int_t, idx), 0, (levels-1)*256)
    gPyramid[0][x,y,k] = beta*(gray[x, y] - level) + level + remap[idx - 256*k]
    for j in range(1,J):
        gPyramid[j][x,y,k] = downsample(gPyramid[j-1])[x,y,k]

    # Get its laplacian pyramid
    lPyramid = [hl.Func('lPyramid%d'%i) for i in range(J)]
    lPyramid[J-1] = gPyramid[J-1]
    for j in range(J-1)[::-1]:
        lPyramid[j][x,y,k] = gPyramid[j][x,y,k] - upsample(gPyramid[j+1])[x,y,k]

    # Make the Gaussian pyramid of the input
    inGPyramid = [hl.Func('inGPyramid%d'%i) for i in range(J)]
    inGPyramid[0] = gray
    for j in range(1,J):
        inGPyramid[j][x,y] = downsample2D(inGPyramid[j-1])[x,y]

    # Make the laplacian pyramid of the output
    outLPyramid = [hl.Func('outLPyramid%d'%i) for i in range(J)]
    for j in range(J):
        # Split input pyramid value into integer and floating parts
        level = inGPyramid[j][x,y]*hl.cast(float_t, levels-1)
        li = hl.clamp(hl.cast(int_t, level), 0, levels-2)
        lf = level - hl.cast(float_t, li)
        # Linearly interpolate between the nearest processed pyramid levels
        outLPyramid[j][x,y] = (1.0-lf)*lPyramid[j][x,y,li] + lf*lPyramid[j][x,y,li+1]

    # Make the Gaussian pyramid of the output
    outGPyramid = [hl.Func('outGPyramid%d'%i) for i in range(J)]
    outGPyramid[J-1] = outLPyramid[J-1]
    for j in range(J-1)[::-1]:
        outGPyramid[j][x,y] = upsample2D(outGPyramid[j+1])[x,y] + outLPyramid[j][x,y]

    # Reintroduce color (Connelly: use eps to avoid scaling up noise w/ apollo3.png input)
    color = hl.Func('color')
    eps = 0.01
    color[x,y,c] = outGPyramid[0][x,y] * (clamped[x,y,c] + eps) / (gray[x,y] + eps)

    output = hl.Func('local_laplacian')
    # Convert back to 16-bit
    output[x,y,c] = hl.cast(hl.UInt(16), hl.clamp(color[x,y,c], 0.0, 1.0) * 65535.0)

    # THE SCHEDULE
    remap.compute_root()

    target = hl.get_target_from_environment()
    if target.has_gpu_feature():
        # GPU Schedule
        print ("Compiling for GPU")
        xi, yi = hl.Var("xi"), hl.Var("yi")
        output.compute_root().gpu_tile(x, y, 32, 32, GPU_Default)
        for j in range(J):
            blockw = 32
            blockh = 16
            if j > 3:
                blockw = 2
                blockh = 2
            if j > 0:
                inGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh, GPU_Default)
            if j > 0:
                gPyramid[j].compute_root().reorder(k, x, y).gpu_tile(x, y, xi, yi, blockw, blockh, GPU_Default)
            outGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh, GPU_Default)
    else:
        # CPU schedule
        print ("Compiling for CPU")
        output.parallel(y, 4).vectorize(x, 4);
        gray.compute_root().parallel(y, 4).vectorize(x, 4);
        for j in range(4):
            if j > 0:
                inGPyramid[j].compute_root().parallel(y, 4).vectorize(x, 4)
            if j > 0:
                gPyramid[j].compute_root().parallel(y, 4).vectorize(x, 4)
            outGPyramid[j].compute_root().parallel(y).vectorize(x, 4)
        for j in range(4,J):
            inGPyramid[j].compute_root().parallel(y)
            gPyramid[j].compute_root().parallel(k)
            outGPyramid[j].compute_root().parallel(y)


    return output


def generate_compiled_file(local_laplacian):

    # Need to copy the process executable from the C++ apps/local_laplacian folder to run this.
    # (after making it of course)
    arguments = ArgumentsVector()
    arguments.append(Argument('levels', False, int_t))
    arguments.append(Argument('alpha', False, float_t))
    arguments.append(Argument('beta', False, float_t))
    arguments.append(Argument('input', True, hl.UInt(16)))
    target = hl.get_target_from_environment()
    local_laplacian.compile_to_file("local_laplacian", arguments, "local_laplacian", target)
    print("Generated compiled file for local_laplacian function.")
    return


def get_input_data():

    image_path = os.path.join(os.path.dirname(__file__), "../../apps/images/rgb.png")
    assert os.path.exists(image_path), \
        "Could not find %s" % image_path
    rgb_data = imread(image_path)
    #print("rgb_data", type(rgb_data), rgb_data.shape, rgb_data.dtype)

    input_data = np.copy(rgb_data.astype(np.uint16), order="F") << 8
    # input data is in range [0, 256*256]
    #print("input_data", type(input_data), input_data.shape, input_data.dtype)

    return input_data


def filter_test_image(local_laplacian, input):

    local_laplacian.compile_jit()

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
    local_laplacian.realize(output_image)

    # save results
    input_path = "local_laplacian_input.png"
    output_path = "local_laplacian.png"
    imsave(input_path, input_data)
    imsave(output_path, output_data)
    print("\nlocal_laplacian realized on output_image.")
    print("Result saved at '", output_path,
          "' ( input data copy at '", input_path, "' ).", sep="")
    return


def main():

    input = hl.ImageParam(hl.UInt(16), 3, 'input')

    # number of intensity levels
    levels = hl.Param(int_t, 'levels', 8)

    #Parameters controlling the filter
    alpha = hl.Param(float_t, 'alpha', 1.0/7.0)
    beta = hl.Param(float_t, 'beta', 1.0)

    local_laplacian = get_local_laplacian(input, levels, alpha, beta)

    generate = False # Set to False to run the jit immediately and get  instant gratification.
    if generate:
        generate_compiled_file(local_laplacian)
    else:
        filter_test_image(local_laplacian, input)

    return

if __name__ == '__main__':
    main()
