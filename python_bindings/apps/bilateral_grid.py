
"Bilateral histogram."

from __future__ import print_function
from __future__ import division


import sys
from halide import *

int_t = Int(32)
float_t = Float(32)

def main():

    input = ImageParam(float_t, 3, 'input')
    r_sigma = Param(float_t, 'r_sigma', 0.1) # Value needed if not generating an executable
    s_sigma = 8 # This is passed during code generation in the C++ version

    x = Var('x')
    y = Var('y')
    z = Var('z')
    c = Var('c')

    # Add a boundary condition
    clamped = Func('clamped')
    clamped[x, y] = input[clamp(x, 0, input.width()-1),
                          clamp(y, 0, input.height()-1),0]

    # Construct the bilateral grid
    r = RDom(0, s_sigma, 0, s_sigma, 'r')
    val = clamped[x * s_sigma + r.x - s_sigma//2, y * s_sigma + r.y - s_sigma//2]
    val = clamp(val, 0.0, 1.0)
    #zi = cast(int_t, val * (1.0/r_sigma) + 0.5)
    zi = cast(int_t, (val / r_sigma) + 0.5)
    histogram = Func('histogram')
    histogram[x, y, z, c] = 0.0
    histogram[x, y, zi, c] += select(c == 0, val, 1.0)

    # Blur the histogram using a five-tap filter
    blurx, blury, blurz = Func('blurx'), Func('blury'), Func('blurz')
    blurz[x, y, z, c] = histogram[x, y, z-2, c] + histogram[x, y, z-1, c]*4 + histogram[x, y, z, c]*6 + histogram[x, y, z+1, c]*4 + histogram[x, y, z+2, c]
    blurx[x, y, z, c] = blurz[x-2, y, z, c] + blurz[x-1, y, z, c]*4 + blurz[x, y, z, c]*6 + blurz[x+1, y, z, c]*4 + blurz[x+2, y, z, c]
    blury[x, y, z, c] = blurx[x, y-2, z, c] + blurx[x, y-1, z, c]*4 + blurx[x, y, z, c]*6 + blurx[x, y+1, z, c]*4 + blurx[x, y+2, z, c]

    # Take trilinear samples to compute the output
    val = clamp(clamped[x, y], 0.0, 1.0)
    zv = val / r_sigma
    zi = cast(int_t, zv)
    zf = zv - zi
    xf = cast(float_t, x % s_sigma) / s_sigma
    yf = cast(float_t, y % s_sigma) / s_sigma
    xi = x/s_sigma
    yi = y/s_sigma
    interpolated = Func('interpolated')
    interpolated[x, y, c] = lerp(lerp(lerp(blury[xi, yi, zi, c], blury[xi+1, yi, zi, c], xf),
                                   lerp(blury[xi, yi+1, zi, c], blury[xi+1, yi+1, zi, c], xf), yf),
                              lerp(lerp(blury[xi, yi, zi+1, c], blury[xi+1, yi, zi+1, c], xf),
                                   lerp(blury[xi, yi+1, zi+1, c], blury[xi+1, yi+1, zi+1, c], xf), yf), zf)

    # Normalize
    bilateral_grid = Func('bilateral_grid')
    bilateral_grid[x, y, c] = interpolated[x, y, 0]/interpolated[x, y, 1]

    target = get_target_from_environment()
    if target.has_gpu_feature():
        # GPU schedule
        # Currently running this directly from the Python code is very slow.
        # Probably because of the dispatch time because generated code
        # is same speed as C++ generated code.
        print ("Compiling for GPU")
        histogram.compute_root().reorder(c, z, x, y).gpu_tile(x, y, 8, 8);
        histogram = histogram.update() # Because returns ScheduleHandle
        histogram.reorder(c, r.x, r.y, x, y).gpu_tile(x, y, 8, 8).unroll(c)
        blurx.compute_root().gpu_tile(x, y, z, 16, 16, 1)
        blury.compute_root().gpu_tile(x, y, z, 16, 16, 1)
        blurz.compute_root().gpu_tile(x, y, z, 8, 8, 4)
        bilateral_grid.compute_root().gpu_tile(x, y, s_sigma, s_sigma)
    else:
        # CPU schedule
        print ("Compiling for CPU")
        histogram.compute_root().parallel(z)
        histogram = histogram.update() # Because returns ScheduleHandle
        histogram.reorder(c, r.x, r.y, x, y).unroll(c)
        blurz.compute_root().reorder(c, z, x, y).parallel(y).vectorize(x, 4).unroll(c)
        blurx.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 4).unroll(c)
        blury.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 4).unroll(c)
        bilateral_grid.compute_root().parallel(y).vectorize(x, 4)

    generate = False # Set to False to run the jit immediately and get  instant gratification.
    if generate:
        # Need to copy the filter executable from the C++ apps/bilateral_grid folder to run this.
        # (after making it of course)
        bilateral_grid.compile_to_file("bilateral_grid", Argument('r_sigma', False, Float(32)), Argument('input', True, UInt(16)), target);
    else:
        eval_func = filter_image(input, bilateral_grid, builtin_image('rgb.png'), disp_time=True)
        I = eval_func()
        if len(sys.argv) >= 2:
            I.save(sys.argv[1])
        else:
            I.show()

if __name__ == '__main__':
    main()
