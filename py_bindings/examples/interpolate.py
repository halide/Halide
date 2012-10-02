import sys; sys.path += ['..', '.']
from halide import *

int_t = Int(32)
float_t = Float(32)

def filter_func(dtype=Float(32), cache={}):
    "Fast interpolation using a pyramid."
    dtype_s = str(dtype).replace('(','').replace(')','')
    if dtype_s in cache:
        return cache[dtype_s]

    s = '_interpolate%s'%dtype_s
    input = UniformImage(dtype, 3, 'input'+s)
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    levels = 10
    
    downsampled = [Func('downsampled%d'%i+s) for i in range(levels)]
    interpolated = [Func('interpolated%d'%i+s) for i in range(levels)]
    level_widths = [Uniform(int_t,'level_widths%d'%i+s) for i in range(levels)]
    level_heights = [Uniform(int_t,'level_heights%d'%i+s) for i in range(levels)]

    downsampled[0][x,y] = (input[x,y,0] * input[x,y,3],
                           input[x,y,1] * input[x,y,3],
                           input[x,y,2] * input[x,y,3],
                           input[x,y,3])
    
    for l in range(1, levels):
        clamped = Func('clamped%d'%l+s)
        clamped[x,y,c] = downsampled[l-1][clamp(cast(int_t,x),cast(int_t,0),cast(int_t,level_widths[l-1]-1)),
                                          clamp(cast(int_t,y),cast(int_t,0),cast(int_t,level_heights[l-1]-1)), c]
        downx = Func('downx%d'%l+s)
        downx[x,y,c] = (clamped[x*2-1,y,c] + 2.0 * clamped[x*2,y,c] + clamped[x*2+1,y,c]) / 4.0
        downsampled[l][x,y,c] = (downx[x,y*2-1,c] + 2.0 * downx[x,y*2,c] + downx[x,y*2+1,c]) / 4.0
        
    interpolated[levels-1][x,y,c] = downsampled[levels-1][x,y,c]
    for l in range(levels-1)[::-1]:
        upsampledx, upsampled = Func('upsampledx%d'%l+s), Func('upsampled%d'%l+s)
        upsampledx[x,y,c] = 0.5 * (interpolated[l+1][x/2 + (x%2),y,c] + interpolated[l+1][x/2,y,c])
        upsampled[x,y,c] = 0.5 * (upsampledx[x, y/2 + (y%2),c] + upsampledx[x,y/2,c])
        interpolated[l][x,y,c] = downsampled[l][x,y,c] + (1.0 - downsampled[l][x,y,3]) * upsampled[x,y,c]

    final = Func('final'+s)
    final[x,y] = (interpolated[0][x,y,0] / interpolated[0][x,y,3],
                  interpolated[0][x,y,1] / interpolated[0][x,y,3],
                  interpolated[0][x,y,2] / interpolated[0][x,y,3],
                  1.0)
    root_all(final)
    
    #print 'interpolate: finished function setup'
    
    def evaluate(in_png):
        #print 'interpolate evaluate'
        width  = in_png.width()
        height = in_png.height()
        print width, height
        for l in range(levels):
            level_widths[l].assign(width)
            level_heights[l].assign(height)
            width = width/2 + 1
            height = height/2 + 1
        print in_png.width(), in_png.height(), 'realizing'
        out = final.realize(in_png.width(), in_png.height(), 4)
        #print 'evaluate realized, returning'
        return out
    
    #print 'interpolate: returning'

    root_all(final)

    ans = (input, final, evaluate, locals())
    cache[dtype_s] = ans
    
    return ans

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, os.path.join(inputs_dir(), 'interpolate_in.png'), eval_func=evaluate)().show()

if __name__ == '__main__':
    main()

    