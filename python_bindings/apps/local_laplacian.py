
"""
Local Laplacian, see e.g. Aubry et al 2011, "Fast and Robust Pyramid-based Image Processing".
"""

import sys
from halide import *

int_t = Int(32)
float_t = Float(32)

def main(J=8):
    downsample_counter=[0]
    upsample_counter=[0]

    x = Var('x')
    y = Var('y')

    def downsample(f):
        downx, downy = Func('downx%d'%downsample_counter[0]), Func('downy%d'%downsample_counter[0])
        downsample_counter[0] += 1

        downx[x,y,c] = (f[2*x-1,y,c] + 3.0*(f[2*x,y,c]+f[2*x+1,y,c]) + f[2*x+2,y,c])/8.0
        downy[x,y,c] = (downx[x,2*y-1,c] + 3.0*(downx[x,2*y,c]+downx[x,2*y+1,c]) + downx[x,2*y+2,c])/8.0

        return downy

    def upsample(f):
        upx, upy = Func('upx%d'%upsample_counter[0]), Func('upy%d'%upsample_counter[0])
        upsample_counter[0] += 1

        upx[x,y,c] = 0.25 * f[(x/2) - 1 + 2*(x%2),y,c] + 0.75 * f[x/2,y,c]
        upy[x,y,c] = 0.25 * upx[x, (y/2) - 1 + 2*(y%2),c] + 0.75 * upx[x,y/2,c]

        return upy

    def downsample2D(f):
        downx, downy = Func('downx%d'%downsample_counter[0]), Func('downy%d'%downsample_counter[0])
        downsample_counter[0] += 1

        downx[x,y] = (f[2*x-1,y] + 3.0*(f[2*x,y]+f[2*x+1,y]) + f[2*x+2,y])/8.0
        downy[x,y] = (downx[x,2*y-1] + 3.0*(downx[x,2*y]+downx[x,2*y+1]) + downx[x,2*y+2])/8.0

        return downy

    def upsample2D(f):
        upx, upy = Func('upx%d'%upsample_counter[0]), Func('upy%d'%upsample_counter[0])
        upsample_counter[0] += 1

        upx[x,y] = 0.25 * f[(x/2) - 1 + 2*(x%2),y] + 0.75 * f[x/2,y]
        upy[x,y] = 0.25 * upx[x, (y/2) - 1 + 2*(y%2)] + 0.75 * upx[x,y/2]

        return upy

    # THE ALGORITHM

    # number of intensity levels
    levels = Param(int_t, 'levels', 8)

    #Parameters controlling the filter
    alpha = Param(float_t, 'alpha', 1.0/7.0)
    beta = Param(float_t, 'beta', 1.0)

    input = ImageParam(UInt(16), 3, 'input')

    # loop variables
    c = Var('c')
    k = Var('k')

    # Make the remapping function as a lookup table.
    remap = Func('remap')
    fx = cast(float_t, x/256.0)
    remap[x] = alpha*fx*exp(-fx*fx/2.0)

    # Convert to floating point
    floating = Func('floating')
    floating[x,y,c] = cast(float_t, input[x,y,c]) / 65535.0

    # Set a boundary condition
    clamped = Func('clamped')
    clamped[x,y,c] = floating[clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c]

    # Get the luminance channel
    gray = Func('gray')
    gray[x,y] = 0.299*clamped[x,y,0] + 0.587*clamped[x,y,1] + 0.114*clamped[x,y,2]

    # Make the processed Gaussian pyramid.
    gPyramid = [Func('gPyramid%d'%i) for i in range(J)]
    # Do a lookup into a lut with 256 entires per intensity level
    level = k / (levels - 1)
    idx = gray[x,y]*cast(float_t, levels-1)*256.0
    idx = clamp(cast(int_t, idx), 0, (levels-1)*256)
    gPyramid[0][x,y,k] = beta*(gray[x, y] - level) + level + remap[idx - 256*k]
    for j in range(1,J):
        gPyramid[j][x,y,k] = downsample(gPyramid[j-1])[x,y,k]

    # Get its laplacian pyramid
    lPyramid = [Func('lPyramid%d'%i) for i in range(J)]
    lPyramid[J-1] = gPyramid[J-1]
    for j in range(J-1)[::-1]:
        lPyramid[j][x,y,k] = gPyramid[j][x,y,k] - upsample(gPyramid[j+1])[x,y,k]

    # Make the Gaussian pyramid of the input
    inGPyramid = [Func('inGPyramid%d'%i) for i in range(J)]
    inGPyramid[0] = gray
    for j in range(1,J):
        inGPyramid[j][x,y] = downsample2D(inGPyramid[j-1])[x,y]

    # Make the laplacian pyramid of the output
    outLPyramid = [Func('outLPyramid%d'%i) for i in range(J)]
    for j in range(J):
        # Split input pyramid value into integer and floating parts
        level = inGPyramid[j][x,y]*cast(float_t, levels-1)
        li = clamp(cast(int_t, level), 0, levels-2)
        lf = level - cast(float_t, li)
        # Linearly interpolate between the nearest processed pyramid levels
        outLPyramid[j][x,y] = (1.0-lf)*lPyramid[j][x,y,li] + lf*lPyramid[j][x,y,li+1]

    # Make the Gaussian pyramid of the output
    outGPyramid = [Func('outGPyramid%d'%i) for i in range(J)]
    outGPyramid[J-1] = outLPyramid[J-1]
    for j in range(J-1)[::-1]:
        outGPyramid[j][x,y] = upsample2D(outGPyramid[j+1])[x,y] + outLPyramid[j][x,y]

    # Reintroduce color (Connelly: use eps to avoid scaling up noise w/ apollo3.png input)
    color = Func('color')
    eps = 0.01
    color[x,y,c] = outGPyramid[0][x,y] * (clamped[x,y,c] + eps) / (gray[x,y] + eps)

    output = Func('local_laplacian')
    # Convert back to 16-bit
    output[x,y,c] = cast(UInt(16), clamp(color[x,y,c], 0.0, 1.0) * 65535.0)

    # THE SCHEDULE
    remap.compute_root()

    target = get_target_from_environment()
    if target.has_gpu_feature():
        # GPU Schedule
        print ("Compiling for GPU")
        output.compute_root().gpu_tile(x, y, 32, 32, GPU_Default)
        for j in range(J):
            blockw = 32
            blockh = 16
            if j > 3:
                blockw = 2
                blockh = 2
            if j > 0:
                inGPyramid[j].compute_root().gpu_tile(x, y, blockw, blockh, GPU_Default)
            if j > 0:
                gPyramid[j].compute_root().reorder(k, x, y).gpu_tile(x, y, blockw, blockh, GPU_Default)
            outGPyramid[j].compute_root().gpu_tile(x, y, blockw, blockh, GPU_Default)
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

    generate = False # Set to False to run the jit immediately and get  instant gratification.
    if generate:
        # Need to copy the process executable from the C++ apps/local_laplacian folder to run this.
        # (after making it of course)
        output.compile_to_file("local_laplacian", Argument('levels', False, int_t), Argument('alpha', False, float_t), Argument('beta', False, float_t), Argument('input', True, UInt(16)), target);
    else:
        I = filter_image(input, output, builtin_image('rgb.png'), disp_time=True)()
        if len(sys.argv) >= 2:
            I.save(sys.argv[1])
        else:
            I.show()

if __name__ == '__main__':
    main()
