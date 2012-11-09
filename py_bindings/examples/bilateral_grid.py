
"Bilateral grid."

import sys; sys.path += ['..', '.']
from halide import *
import autotune
import valid_schedules
import random

int_t = Int(32)
float_t = Float(32)

def filter_func(dtype=UInt(16), use_uniforms=False):
    def lerp(a, b, alpha):
        return (1.0-alpha)*a + alpha*b

    input = UniformImage(float_t, 3, 'input')
    if use_uniforms:
        r_sigma = Uniform(float_t, 0.1)
    else:
        r_sigma = 0.1
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
        grid.root().parallel(z)
        grid.update().reorder(c, x, y).parallel(y)
        blurx.root().parallel(z).vectorize(x, 4)
        blury.root().parallel(z).vectorize(x, 4)
        blurz.root().parallel(z).vectorize(x, 4)
        smoothed.root().parallel(y).vectorize(x, 4)
    elif schedule == 2:
        # Best schedule for GPU
        gridz = grid.arg(2)
        grid.root().cudaTile(x, y, 16, 16)
        grid.update().root().cudaTile(x, y, 16, 16)
        blurx.root().cudaTile(x, y, 8, 8)
        blury.root().cudaTile(x, y, 8, 8)
        blurz.root().cudaTile(x, y, 8, 8)
        smoothed.root().cudaTile(x, y, s_sigma, s_sigma)
    else:
        raise ValueError
    
    tune_ref_schedules = {'human': 'grid.root().parallel(z).update().reorder(c, x, y).parallel(y)\n' +
                                   'blurx.root().parallel(z).vectorize(x, 4)\n' +
                                   'blury.root().parallel(z).vectorize(x, 4)\n' +
                                   'blurz.root().parallel(z).vectorize(x, 4)\n' +
                                   'smoothed.root().parallel(y).vectorize(x, 4)\n'}
    # GPU
    gpu_human = 'grid.root().cudaTile(x, y, 16, 16).update().root().cudaTile(x, y, 16, 16)\n' + \
                'blurx.root().cudaTile(x, y, 8, 8)\n' + \
                'blury.root().cudaTile(x, y, 8, 8)\n' + \
                'blurz.root().cudaTile(x, y, 8, 8)\n' + \
                'smoothed.root().cudaTile(x, y, 8, 8)\n'
    if autotune.is_cuda() and False:
        tune_ref_schedules['human'] = gpu_human


    #autotune.print_tunables(smoothed)
    #for i in range(123,10000):
    #    random.seed(i)
    #    print '-'*40
    #    print 'Schedule %d'%i
    #    p = autotune.AutotuneParams()
    #    print valid_schedules.random_schedule(smoothed, p.min_depth, p.max_depth)

#    std::vector<Func::Arg> args;
#    args.push_back(r_sigma);
#    args.push_back(input);
#    smoothed.compileToFile("bilateral_grid", args);
    return (input, smoothed, None, locals())
    
def main(is_sat=False):
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo3.png'), disp_time=True)().show()
    
if __name__ == '__main__':
    main()
