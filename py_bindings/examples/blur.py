import sys; sys.path += ['..', '.']
from halide import *

def filter_func(dtype=UInt(16), cache={}):
    "Simple 3x3 blur."
    dtype_s = str(dtype).replace('(','').replace(')','')
    if dtype_s in cache:
        return cache[dtype_s]

    s = '_blur%s'%dtype_s
    input = UniformImage(dtype, 3, 'input'+s)
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    input_clamped = Func('input_clamped'+s)
    blur_x = Func('blur_x'+s)
    blur_y = Func('blur_y'+s)

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)),
                                 clamp(c,cast(Int(32),0),cast(Int(32),2))]
    blur_x[x,y,c] = (input_clamped[x-1,y,c]/4+input_clamped[x,y,c]/4+input_clamped[x+1,y,c]/4)/3
    blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4
    
    ans = (input, blur_y, None, locals())
    cache[dtype_s] = ans
    
    return ans

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, os.path.join(inputs_dir(), 'lena_crop.png'))().show()

if __name__ == '__main__':
    main()

    