import sys; sys.path += ['..', '.']
import time
from halide import *
import autotune

int_t = Int(32)
float_t = Float(32)

def filter_func(dtype=Float(32), use_uniforms=False, in_filename=os.path.join(inputs_dir(), 'interpolate_large.png')):
    "Fast interpolation using a pyramid."

    input = UniformImage(dtype, 3, 'input')
    x = Var('x')
    y = Var('y')
    c = Var('c')
    levels = 10
    
    downsampled = [Func('d%d'%i) for i in range(levels)]
    interpolated = [Func('i%d'%i) for i in range(levels)]

    clamped = Func('clamped')
    clamped[c, x, y] = input[clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c];

    downsampled[0][c,x,y] = select(c<3, clamped[c,x,y] * clamped[3,x,y], clamped[3,x,y])

    
    downx = [None] + [Func('dx%d'%l) for l in range(1,levels)]
    for l in range(1, levels):
        downx[l][c,x,y] = (downsampled[l-1][c,x*2-1,y] + 2.0 * downsampled[l-1][c,x*2,y] + downsampled[l-1][c,x*2+1,y]) * 0.25
        downsampled[l][c,x,y] = (downx[l][c,x,y*2-1] + 2.0 * downx[l][c,x,y*2] + downx[l][c,x,y*2+1]) * 0.25
    
    upsampled = [Func('u%d'%l) for l in range(levels-1)]
    upsampledx = [Func('ux%d'%l) for l in range(levels-1)]

    interpolated[levels-1][c,x,y] = downsampled[levels-1][c,x,y]
    for l in range(levels-1)[::-1]:
        upsampledx[l][c,x,y] = 0.5 * (interpolated[l+1][c, x/2 + (x%2),y] + interpolated[l+1][c,x/2,y])
        upsampled[l][c,x,y] = 0.5 * (upsampledx[l][c, x, y/2 + (y%2)] + upsampledx[l][c,x,y/2])
        interpolated[l][c,x,y] = downsampled[l][c,x,y] + (1.0 - downsampled[l][3,x,y]) * upsampled[l][c,x,y]

    final = Func('final')
    final[x,y,c] = interpolated[0][c,x,y] / interpolated[0][3,x,y]
    
    def evaluate(in_png):
        T0 = time.time()

        out = final.realize(in_png.width(), in_png.height(), 3)
        print 'Interpolated in %.5f secs' % (time.time()-T0)

        return out
    
    # Special tuning variables interpreted by the autotuner
    tune_out_dims = (1408, 1408, 3)
    tune_in_images = [in_filename]
    tune_image_ext = '.ppm'

    human_schedule = 'final.root().parallel(y).bound(c, 0, 3)\n'
    for i in range(1, levels-1):
        human_schedule += 'd%d.root().vectorize(c, 4).parallel(y)\n'%i
        human_schedule += 'i%d.root().vectorize(c, 4).parallel(y)\n'%i
        
    tune_ref_schedules = {'human': human_schedule}
#    tune_constraints = 'final.bound(c, 0, 3)'
    tune_constraints = autotune.bound_recursive(final, 'c', 0, 3)
   
    autotune.Schedule.fromstring(final, human_schedule).apply()
    
    return (input, final, evaluate, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, local_d['tune_in_images'][0], eval_func=evaluate, out_dims=local_d['tune_out_dims'])().show()
#    filter_image(input, out_func, os.path.join(inputs_dir(), 'interpolate_in.png'))().show()

if __name__ == '__main__':
    main()

    
