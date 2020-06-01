"""
Local Laplacian, see e.g. Aubry et al 2011, "Fast and Robust Pyramid-based Image Processing".
"""

import halide as hl

import numpy as np
import imageio
import os.path

int_t = hl.Int(32)
float_t = hl.Float(32)


def get_local_laplacian(input, levels, alpha, beta, J=8):
    n_downsamples = 0
    n_upsamples = 0

    x = hl.Var('x')
    y = hl.Var('y')

    def downsample(f):
        nonlocal n_downsamples
        downx, downy = hl.Func(f'downx{n_downsamples}'), hl.Func(f'downy{n_downsamples}')
        n_downsamples += 1

        downx[x, y, c] = (f[2 * x - 1, y, c] + 3.0 * (f[2 * x, y, c] + f[2 * x + 1, y, c]) + f[2 * x + 2, y, c]) / 8.0
        downy[x, y, c] = (downx[x, 2 * y - 1, c] + 3.0 * (downx[x, 2 * y, c] + downx[x, 2 * y + 1, c])
                          + downx[x, 2 * y + 2, c]) / 8.0

        return downy

    def upsample(f):
        nonlocal n_upsamples
        upx, upy = hl.Func(f'upx{n_upsamples}'), hl.Func(f'upy{n_upsamples}')
        n_upsamples += 1

        upx[x, y, c] = 0.25 * f[(x // 2) - 1 + 2 * (x % 2), y, c] + 0.75 * f[x // 2, y, c]
        upy[x, y, c] = 0.25 * upx[x, (y // 2) - 1 + 2 * (y % 2), c] + 0.75 * upx[x, y // 2, c]

        return upy

    def downsample2D(f):
        nonlocal n_downsamples
        downx, downy = hl.Func(f'downx{n_downsamples}'), hl.Func(f'downy{n_downsamples}')
        n_downsamples += 1

        downx[x, y] = (f[2 * x - 1, y] + 3.0 * (f[2 * x, y] + f[2 * x + 1, y]) + f[2 * x + 2, y]) / 8.0
        downy[x, y] = (downx[x, 2 * y - 1] + 3.0 * (downx[x, 2 * y] + downx[x, 2 * y + 1]) + downx[x, 2 * y + 2]) / 8.0

        return downy

    def upsample2D(f):
        nonlocal n_upsamples
        upx, upy = hl.Func(f'upx{n_upsamples}'), hl.Func(f'upy{n_upsamples}')
        n_upsamples += 1

        upx[x, y] = 0.25 * f[(x // 2) - 1 + 2 * (x % 2), y] + 0.75 * f[x // 2, y]
        upy[x, y] = 0.25 * upx[x, (y // 2) - 1 + 2 * (y % 2)] + 0.75 * upx[x, y // 2]

        return upy

    # THE ALGORITHM

    # loop variables
    c = hl.Var('c')
    k = hl.Var('k')

    # Make the remapping function as a lookup table.
    remap = hl.Func('remap')
    fx = hl.cast(float_t, x / 256.0)
    # remap[x] = alpha*fx*exp(-fx*fx/2.0)
    remap[x] = alpha * fx * hl.exp(-fx * fx / 2.0)

    # Convert to floating point
    floating = hl.Func('floating')
    floating[x, y, c] = hl.cast(float_t, input[x, y, c]) / 65535.0

    # Set a boundary condition
    clamped = hl.Func('clamped')
    clamped[x, y, c] = floating[hl.clamp(x, 0, input.width() - 1), hl.clamp(y, 0, input.height() - 1), c]

    # Get the luminance channel
    gray = hl.Func('gray')
    kR = hl.f32(0.299)
    kG = hl.f32(0.587)
    kB = hl.f32(0.114)
    gray[x, y] = kR * clamped[x, y, 0] + kG * clamped[x, y, 1] + kB * clamped[x, y, 2]

    # Make the processed Gaussian pyramid.
    gPyramid = [hl.Func(f'gPyramid{i}') for i in range(J)]
    # Do a lookup into a lut with 256 entires per intensity level
    level = k / (levels - 1)
    idx = gray[x, y] * hl.cast(float_t, levels - 1) * 256.0
    idx = hl.clamp(hl.cast(int_t, idx), 0, (levels - 1) * 256)
    gPyramid[0][x, y, k] = beta * (gray[x, y] - level) + level + remap[idx - 256 * k]
    for j in range(1, J):
        gPyramid[j][x, y, k] = downsample(gPyramid[j - 1])[x, y, k]

    # Get its laplacian pyramid
    lPyramid = [hl.Func(f'lPyramid{i}') for i in range(J)]
    lPyramid[J - 1] = gPyramid[J - 1]
    for j in range(J - 1)[::-1]:
        lPyramid[j][x, y, k] = gPyramid[j][x, y, k] - upsample(gPyramid[j + 1])[x, y, k]

    # Make the Gaussian pyramid of the input
    inGPyramid = [hl.Func(f'inGPyramid{i}') for i in range(J)]
    inGPyramid[0] = gray
    for j in range(1, J):
        inGPyramid[j][x, y] = downsample2D(inGPyramid[j - 1])[x, y]

    # Make the laplacian pyramid of the output
    outLPyramid = [hl.Func(f'outLPyramid{i}') for i in range(J)]
    for j in range(J):
        # Split input pyramid value into integer and floating parts
        level = inGPyramid[j][x, y] * hl.cast(float_t, levels - 1)
        li = hl.clamp(hl.cast(int_t, level), 0, levels - 2)
        lf = level - hl.cast(float_t, li)
        # Linearly interpolate between the nearest processed pyramid levels
        outLPyramid[j][x, y] = (1.0 - lf) * lPyramid[j][x, y, li] + lf * lPyramid[j][x, y, li + 1]

    # Make the Gaussian pyramid of the output
    outGPyramid = [hl.Func(f'outGPyramid{i}') for i in range(J)]
    outGPyramid[J - 1] = outLPyramid[J - 1]
    for j in range(J - 1)[::-1]:
        outGPyramid[j][x, y] = upsample2D(outGPyramid[j + 1])[x, y] + outLPyramid[j][x, y]

    # Reintroduce color (Connelly: use eps to avoid scaling up noise w/ apollo3.png input)
    color = hl.Func('color')
    eps = hl.f32(0.01)
    color[x, y, c] = outGPyramid[0][x, y] * (clamped[x, y, c] + eps) / (gray[x, y] + eps)

    output = hl.Func('local_laplacian')
    # Convert back to 16-bit
    output[x, y, c] = hl.cast(hl.UInt(16), hl.clamp(color[x, y, c], 0.0, 1.0) * 65535.0)

    # THE SCHEDULE
    target = hl.get_target_from_environment()
    if target.has_gpu_feature():
        # GPU Schedule
        print("Compiling for GPU")
        xi, yi = hl.Var("xi"), hl.Var("yi")

        remap.compute_root()
        output.compute_root().gpu_tile(x, y, xi, yi, 16, 8)
        for j in range(J):
            blockw = 16
            blockh = 8
            if j > 3:
                blockw = 2
                blockh = 2
            if j > 0:
                inGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh)
                gPyramid[j].compute_root().reorder(k, x, y).gpu_tile(x, y, xi, yi, blockw, blockh)
            outGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh)
    else:
        # CPU schedule
        print("Compiling for CPU")

        remap.compute_root()
        output.parallel(y, 4).vectorize(x, 4)
        gray.compute_root().parallel(y, 4).vectorize(x, 4)
        for j in range(4):
            if j > 0:
                inGPyramid[j].compute_root().parallel(y, 4).vectorize(x, 4)
            if j > 0:
                gPyramid[j].compute_root().parallel(y, 4).vectorize(x, 4)
            outGPyramid[j].compute_root().parallel(y).vectorize(x, 4)
        for j in range(4, J):
            inGPyramid[j].compute_root().parallel(y)
            gPyramid[j].compute_root().parallel(k)
            outGPyramid[j].compute_root().parallel(y)

    return output


def get_input_data():
    image_path = os.path.join(os.path.dirname(__file__), "../../apps/images/rgb.png")
    assert os.path.exists(image_path), f"Could not find {image_path}"

    rgb_data = imageio.imread(image_path)

    # input data is in range [0, 256*256]
    input_data = rgb_data.astype(np.uint16, order="F") << 8
    return input_data


def filter_test_image(local_laplacian, input):
    local_laplacian.compile_jit(hl.get_target_from_environment())

    # preparing input and output memory buffers (numpy ndarrays)
    input_data = get_input_data()
    input_image = hl.Buffer(input_data)
    input.set(input_image)

    output_data = np.empty_like(input_data)

    # do the actual computation
    input_width, input_height = input_data.shape[:2]
    output_image = local_laplacian.realize(input_width, input_height, 3)
    output_data = np.asanyarray(output_image)

    # convert back to uint8
    input_data = (input_data >> 8).astype(np.uint8)
    output_data = (output_data >> 8).astype(np.uint8)

    # save results
    input_path = "local_laplacian_input.png"
    output_path = "local_laplacian.png"

    imageio.imsave(input_path, input_data)
    imageio.imsave(output_path, output_data)

    print()
    print("local_laplacian realized on output_image.")
    print(f'Result saved at {output_path} (input data copy at {input_path}).')


def main():
    input_img = hl.ImageParam(hl.UInt(16), 3, 'input')

    # number of intensity levels
    levels = hl.Param(int_t, 'levels', 8)

    # Parameters controlling the filter
    alpha = hl.Param(float_t, 'alpha', 1.0 / 7.0)
    beta = hl.Param(float_t, 'beta', 1.0)

    local_laplacian = get_local_laplacian(input_img, levels, alpha, beta)

    filter_test_image(local_laplacian, input_img)


if __name__ == '__main__':
    main()
