import sys; sys.path += ['..', '.']
from halide import *

int_t = Int(32)
float_t = Float(32)

def boxblur_mode(dtype=UInt(16), is_sat=True, cache={}):
    "Box blur, either with summed-area table (default) or cumsum"
    dtype_s = str(int(is_sat)) + str(dtype).replace('(','').replace(')','')
    if dtype_s in cache:
        return cache[dtype_s]

    s = '_box%s'%dtype_s
    input = UniformImage(dtype, 3, 'input'+s)
    box_size = Uniform(int_t, 'box_size'+s, 15)

    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    
    sum = Func('sum'+s)                     # Cumulative sum in x and y or summed area table
    zero = cast(int_t,0)
    w1 = cast(int_t,input.width()-1)
    h1 = cast(int_t,input.height()-1)
    if is_sat:
        r = RDom(0,input.width(),0,input.height(),'r'+s)
        rx = r.x
        ry = r.y
        sum[x,y,c] = cast(float_t if dtype.isFloat() else int_t, input[x,y,c])
        #sum[rx,ry,c] = sum[rx,ry,c] + sum[max(rx-1,zero),ry,c] + sum[rx,max(ry-1,zero),c] - sum[max(rx-1,zero),max(ry-1,zero),c]
        sum[rx,ry,c] += sum[max(rx-1,zero),ry,c] + sum[rx,max(ry-1,zero),c] - sum[max(rx-1,zero),max(ry-1,zero),c]
    else:
        rx = RDom(0,input.width(),'rx'+s)
        ry = RDom(0,input.height(),'ry'+s)
        sumx = Func('sumx'+s)
        sumx[x,y,c] = cast(float_t if dtype.isFloat() else int_t, input[x,y,c])
        sumx[rx,y,c] += sumx[max(rx-1,zero),y,c]
        sum[x,y,c] = sumx[x,y,c]
        sum[x,ry,c] += sum[x,max(ry-1,zero),c]
    sum_clamped = Func('sum_clamped'+s)
    sum_clamped[x,y,c] = sum[clamp(x,zero,w1),clamp(y,zero,h1),c]
    
    weight = Func('weight'+s)
    weight[x,y] = ((min(x+box_size,w1)-max(x-box_size-1,zero))*
                   (min(y+box_size,h1)-max(y-box_size-1,zero)))
    
    output = Func('output'+s)
    output[x,y,c] = cast(dtype,
                    (sum_clamped[x+box_size  ,y+box_size  ,c]-
                     sum_clamped[x-box_size-1,y+box_size  ,c]-
                     sum_clamped[x+box_size  ,y-box_size-1,c]+
                     sum_clamped[x-box_size-1,y-box_size-1,c])/weight[x,y])

    ans = (input, output, None, locals())
    cache[dtype_s] = ans
    
    return ans

def boxblur_sat(dtype=UInt(16)):
    "Box blur (implemented with summed area table)."
    return boxblur_mode(dtype, True)
    
def boxblur_cumsum(dtype=UInt(16)):
    "Box blur (implemented with cumsum)."
    return boxblur_mode(dtype, False)

def main():
    args = sys.argv[1:]
    is_sat    = len(args) == 1 and args[0] == 'sat'
    is_cumsum = len(args) == 1 and args[0] == 'cumsum'
    if not (is_sat or is_cumsum):
        print 'Usage: python boxblur.py sat|cumsum'
        sys.exit(1)
    if is_sat:
        (input, out_func, evaluate, local_d) = boxblur_sat()
    if is_cumsum:
        (input, out_func, evaluate, local_d) = boxblur_cumsum()
    filter_image(input, out_func, os.path.join(inputs_dir(), 'lena_crop.png'))().show()

if __name__ == '__main__':
    main()

    