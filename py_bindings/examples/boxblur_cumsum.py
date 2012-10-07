
"Box blur in cumulative sum mode."

import sys; sys.path += ['..', '.']
from halide import *

int_t = Int(32)
float_t = Float(32)

def boxblur_mode(dtype=UInt(16), is_sat=False, use_uniforms=False):
    "Box blur, either with summed-area table or cumsum (default)"

    input = UniformImage(dtype, 3, 'input')
    if use_uniforms:
        box_size = Uniform(int_t, 'box_size', 15)
    else:
        box_size = 15                                   # Avoid uniform to make default_runner happy

    x = Var('x')
    y = Var('y')
    c = Var('c')
    
    sum = Func('sum')                     # Cumulative sum in x and y or summed area table
    zero = cast(int_t,0)
    w1 = cast(int_t,input.width()-1)
    h1 = cast(int_t,input.height()-1)
    if is_sat:
        r = RDom(0,input.width(),0,input.height(),'r')
        rx = r.x
        ry = r.y
        sum[x,y,c] = cast(float_t if dtype.isFloat() else int_t, input[x,y,c])
        #sum[rx,ry,c] = sum[rx,ry,c] + sum[max(rx-1,zero),ry,c] + sum[rx,max(ry-1,zero),c] - sum[max(rx-1,zero),max(ry-1,zero),c]
        sum[rx,ry,c] += sum[max(rx-1,zero),ry,c] + sum[rx,max(ry-1,zero),c] - sum[max(rx-1,zero),max(ry-1,zero),c]
    else:
        rx = RDom(0,input.width(),'rx')
        ry = RDom(0,input.height(),'ry')
        sumx = Func('sumx')
        sumx[x,y,c] = cast(float_t if dtype.isFloat() else int_t, input[x,y,c])
        sumx[rx,y,c] += sumx[max(rx-1,zero),y,c]
        sum[x,y,c] = sumx[x,y,c]
        sum[x,ry,c] += sum[x,max(ry-1,zero),c]
    sum_clamped = Func('sum_clamped')
    sum_clamped[x,y,c] = sum[clamp(x,zero,w1),clamp(y,zero,h1),c]
    
    weight = Func('weight')
    weight[x,y] = ((min(x+box_size,w1)-max(x-box_size-1,zero))*
                   (min(y+box_size,h1)-max(y-box_size-1,zero)))
    
    output = Func('output')
    output[x,y,c] = cast(dtype,
                    (sum_clamped[x+box_size  ,y+box_size  ,c]-
                     sum_clamped[x-box_size-1,y+box_size  ,c]-
                     sum_clamped[x+box_size  ,y-box_size-1,c]+
                     sum_clamped[x-box_size-1,y-box_size-1,c])/weight[x,y])

    return (input, output, None, locals())

def filter_func(dtype=UInt(16)):
    return boxblur_mode(dtype)
    
def main(is_sat=False):
    (input, out_func, evaluate, local_d) = boxblur_mode(is_sat=is_sat)
    filter_image(input, out_func, os.path.join(inputs_dir(), 'lena_crop.png'))().show()

if __name__ == '__main__':
    main()

