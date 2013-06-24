
"Bilateral grid."

import sys
from halide import *

int_t = Int(32)
float_t = Float(32)

def main():
    def lerp(a, b, alpha):
        return (1.0-alpha)*a + alpha*b
    
    input = ImageParam(float_t, 3, 'input')
    r_sigma = Param(float_t, 0.1)
    s_sigma = 8
    
    x = Var('x')
    y = Var('y')
    z = Var('z')
    c = Var('c')

    clamped = Func('clamped')
    clamped[x, y] = input[clamp(x, 0, input.width()-1),
                          clamp(y, 0, input.height()-1),0]

    r = RDom(0, s_sigma, 0, s_sigma, 'r')
    val = clamped[x * s_sigma + r.x - s_sigma/2, y * s_sigma + r.y - s_sigma/2]
    val = clamp(val, 0.0, 1.0)
    zi = cast(int_t, val * (1.0/r_sigma) + 0.5)
    grid = Func('grid')
    grid[x, y, z, c] = 0.0
    grid[x, y, zi, c] += select(c == 0, val, 1.0)

    # Blur the grid using a five-tap filter
    blurx, blury, blurz = Func('blurx'), Func('blury'), Func('blurz')
    blurx[x, y, z] = grid[x-2, y, z] + grid[x-1, y, z]*4 + grid[x, y, z]*6 + grid[x+1, y, z]*4 + grid[x+2, y, z]
    blury[x, y, z] = blurx[x, y-2, z] + blurx[x, y-1, z]*4 + blurx[x, y, z]*6 + blurx[x, y+1, z]*4 + blurx[x, y+2, z]
    blurz[x, y, z] = blury[x, y, z-2] + blury[x, y, z-1]*4 + blury[x, y, z]*6 + blury[x, y, z+1]*4 + blury[x, y, z+2]

    # Take trilinear samples to compute the output
    val = clamp(clamped[x, y], 0.0, 1.0)
    zv = val * (1.0/r_sigma)
    zi = cast(int_t, zv)
    zf = zv - zi
    xf = cast(float_t, x % s_sigma) / s_sigma
    yf = cast(float_t, y % s_sigma) / s_sigma
    xi = x/s_sigma
    yi = y/s_sigma
    interpolated = Func('interpolated')
    interpolated[x, y] = lerp(lerp(lerp(blurz[xi, yi, zi], blurz[xi+1, yi, zi], xf),
                                   lerp(blurz[xi, yi+1, zi], blurz[xi+1, yi+1, zi], xf), yf),
                              lerp(lerp(blurz[xi, yi, zi+1], blurz[xi+1, yi, zi+1], xf),
                                   lerp(blurz[xi, yi+1, zi+1], blurz[xi+1, yi+1, zi+1], xf), yf), zf)

    # Normalize
    smoothed = Func('smoothed')
    smoothed[x, y, c] = interpolated[x, y, 0]/interpolated[x, y, 1]

    schedule = 1
    if schedule == 0:
        pass
    elif schedule == 1:
        # Best schedule for CPU
        grid.compute_root().parallel(z)
        #grid.update().reorder(c, x, y).parallel(y)         # This fails with SEGFAULT
        blurx.compute_root().parallel(z).vectorize(x, 4)
        blury.compute_root().parallel(z).vectorize(x, 4)
        blurz.compute_root().parallel(z).vectorize(x, 4)
        smoothed.compute_root().parallel(y).vectorize(x, 4)
    elif schedule == 2:
        # Best schedule for GPU
        gridz = grid.arg(2)
        grid.compute_root().cudaTile(x, y, 16, 16)
        grid.update().root().cudaTile(x, y, 16, 16)
        blurx.compute_root().cudaTile(x, y, 8, 8)
        blury.compute_root().cudaTile(x, y, 8, 8)
        blurz.compute_root().cudaTile(x, y, 8, 8)
        smoothed.compute_root().cudaTile(x, y, s_sigma, s_sigma)
    else:
        raise ValueError
    
    eval_func = filter_image(input, smoothed, builtin_image('rgb.png'), disp_time=True)
    I = eval_func()
    if len(sys.argv) >= 2:
        I.save(sys.argv[1])
    else:
        I.show()
    
if __name__ == '__main__':
    main()
