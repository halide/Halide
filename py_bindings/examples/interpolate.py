import sys; sys.path += ['..', '.']
import time
from halide import *

int_t = Int(32)
float_t = Float(32)

def filter_func(dtype=Float(32), use_uniforms=False, in_filename=os.path.join(inputs_dir(), 'interpolate_large.png')):
    "Fast interpolation using a pyramid."

    input = UniformImage(dtype, 3, 'input')
    x = Var('x')
    y = Var('y')
    c = Var('c')
    levels = 10
    
    def pyramid_sizes(w, h):
        ans = []
        for l in range(levels):
            ans.append((w, h))
            w = w/2 + 1
            h = h/2 + 1
        return ans
    
    downsampled = [Func('downsampled%d'%i) for i in range(levels)]
    interpolated = [Func('interpolated%d'%i) for i in range(levels)]
    if use_uniforms:
        level_widths = [Uniform(int_t,'level_widths%d'%i) for i in range(levels)]
        level_heights = [Uniform(int_t,'level_heights%d'%i) for i in range(levels)]
    else:
        I = Image(dtype, in_filename)
        sizes = pyramid_sizes(I.width(), I.height())
        level_widths = [sz[0] for sz in sizes]
        level_heights = [sz[1] for sz in sizes]
    downsampled[0][x,y,c] = select(c<3, input[x,y,c] * input[x,y,3], input[x,y,3])
    
    clamped = [None] + [Func('clamped%d'%l) for l in range(1,levels)]
    downx = [None] + [Func('downx%d'%l) for l in range(1,levels)]
    for l in range(1, levels):
        clamped[l][x,y,c] = downsampled[l-1][clamp(cast(int_t,x),cast(int_t,0),cast(int_t,level_widths[l-1]-1)),
                                             clamp(cast(int_t,y),cast(int_t,0),cast(int_t,level_heights[l-1]-1)), c]
        downx[l][x,y,c] = (clamped[l][x*2-1,y,c] + 2.0 * clamped[l][x*2,y,c] + clamped[l][x*2+1,y,c]) / 4.0
        downsampled[l][x,y,c] = (downx[l][x,y*2-1,c] + 2.0 * downx[l][x,y*2,c] + downx[l][x,y*2+1,c]) / 4.0
    
    upsampled = [Func('upsampled%d'%l) for l in range(levels-1)]
    upsampledx = [Func('upsampledx%d'%l) for l in range(levels-1)]
    
    interpolated[levels-1][x,y,c] = downsampled[levels-1][x,y,c]
    for l in range(levels-1)[::-1]:
        upsampledx[l][x,y,c] = 0.5 * (interpolated[l+1][x/2 + (x%2),y,c] + interpolated[l+1][x/2,y,c])
        upsampled[l][x,y,c] = 0.5 * (upsampledx[l][x, y/2 + (y%2),c] + upsampledx[l][x,y/2,c])
        interpolated[l][x,y,c] = downsampled[l][x,y,c] + (1.0 - downsampled[l][x,y,3]) * upsampled[l][x,y,c]

    final = Func('final')
    final[x,y,c] = select(c<3, interpolated[0][x,y,c] / interpolated[0][x,y,3], 1.0)
    
    def evaluate(in_png):
        T0 = time.time()
        if use_uniforms:
            sizes = pyramid_sizes(in_png.width(), in_png.height())
            for l in range(levels):
                level_widths[l].assign(sizes[l][0])
                level_heights[l].assign(sizes[l][1])

        out = final.realize(in_png.width(), in_png.height(), 4)
        print 'Interpolated in %.5f secs' % (time.time()-T0)

        return out
    
    # Special tuning variables interpreted by the autotuner
    tune_in_images = [in_filename]
    tune_image_ext = '.png'             # .ppm does not support 4 channel images

    human_schedule = 'final.root().tile(x,y,_c0,_c1,4,4).parallel(y).vectorize(_c0,4)\n'
    for i in range(levels):
        human_schedule += 'downsampled%d.root().tile(x,y,_c0,_c1,4,4).parallel(y).vectorize(_c0,4).unroll(_c1,4)\n'%i
        if i < levels-1:
            human_schedule += 'upsampled%d.chunk(x)\n'%i
        human_schedule += 'interpolated%d.root().tile(x,y,_c0,_c1,4,4).parallel(y).vectorize(_c0,4).unroll(_c1,4)\n'%i
        
    tune_ref_schedules = {'human': human_schedule}
    
    import autotune
    autotune.Schedule.fromstring(final, human_schedule).apply()
    
    return (input, final, evaluate, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, local_d['tune_in_images'][0], eval_func=evaluate)().show()
#    filter_image(input, out_func, os.path.join(inputs_dir(), 'interpolate_in.png'))().show()

if __name__ == '__main__':
    main()

    
