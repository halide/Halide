import sys; sys.path += ['..', '.']
from halide import *

def filter_func(dtype=UInt(16)):
    "Simple 3x3 blur."
    input = UniformImage(dtype, 3, 'input')
    x = Var('x')
    y = Var('y')
    c = Var('c')
    input_clamped = Func('input_clamped')
    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)),
                                 clamp(c,cast(Int(32),0),cast(Int(32),2))]
    blur_x[x,y,c] = (input_clamped[x-1,y,c]/4+input_clamped[x,y,c]/4+input_clamped[x+1,y,c]/4)/3
    blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4
    
    return (input, blur_y, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()

    x, y, c = local_d['x'], local_d['y'], local_d['c']
    blur_x, blur_y = local_d['blur_x'], local_d['blur_y']

    xi, yi = Var('xi'), Var('yi')

    if 0:
        blur_x.root().vectorize(x, 8)
        blur_y.vectorize(x, 8)
    else:
        blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
        blur_x.chunk(x).vectorize(x, 8)

    test = filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo2.png'), disp_time=True)
    for i in range(5):
        test()
    test().show()

if __name__ == '__main__':
    main()

    
