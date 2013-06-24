
"Fast image interpolation using a pyramid."

import time, sys
from halide import *

int_t = Int(32)
float_t = Float(32)

def main(dtype=Float(32)):
    input = ImageParam(dtype, 3, 'input')
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
    level_widths = [Param(int_t,'level_widths%d'%i) for i in range(levels)]
    level_heights = [Param(int_t,'level_heights%d'%i) for i in range(levels)]

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
    
    # Schedule: flat schedule with parallelization + vectorization
    xi, yi = Var('xi'), Var('yi')
    for l in range(1, levels):    
        clamped[l].compute_root().parallel(y).bound(c, 0, 4).reorder(c, x, y).reorder_storage(c, x, y).vectorize(c, 4)
        if l > 0: downsampled[l].compute_root().parallel(y).reorder(c, x, y).reorder_storage(c, x, y).vectorize(c, 4)
        interpolated[l].compute_root().parallel(y).reorder(c, x, y).reorder_storage(c, x, y).vectorize(c, 4)
        interpolated[l].unroll(x, 2).unroll(y, 2)
        
    final.reorder(c, x, y).bound(c, 0, 3).parallel(y)
    final.tile(x, y, xi, yi, 2, 2).unroll(xi).unroll(yi)
    final.bound(x, 0, input.width())
    final.bound(y, 0, input.height())

    def evaluate(in_png):
        T0 = time.time()
        sizes = pyramid_sizes(in_png.width(), in_png.height())
        for l in range(levels):
            level_widths[l].set(sizes[l][0])
            level_heights[l].set(sizes[l][1])

        out = final.realize(in_png.width(), in_png.height(), 3)
        print 'Interpolated in %.5f secs' % (time.time()-T0)

        return out
        
    I = filter_image(input, final, builtin_image('rgba.png'), eval_func=evaluate)()
    if len(sys.argv) >= 2:
        I.save(sys.argv[1])
    else:
        I.show()

if __name__ == '__main__':
    main()

