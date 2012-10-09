import sys; sys.path += ['..', '.']
from halide import *

def filter_func(dtype=UInt(16)):
    "Erode on 5x5 stencil, first erode x then erode y."
    input = UniformImage(dtype, 3, 'input')
    x = Var('x')
    y = Var('y')
    c = Var('c')
    input_clamped = Func('input_clamped')
    erode_x = Func('erode_x')
    erode_y = Func('erode_y')

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    erode_x[x,y,c] = min(min(min(min(input_clamped[x-2,y,c],input_clamped[x-1,y,c]),input_clamped[x,y,c]),input_clamped[x+1,y,c]),input_clamped[x+2,y,c])
    erode_y[x,y,c] = min(min(min(min(erode_x[x,y-2,c],erode_x[x,y-1,c]),erode_x[x,y,c]),erode_x[x,y+1,c]),erode_x[x,y+2,c])
    return (input, erode_y, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    
    x, y, c = local_d['x'], local_d['y'], local_d['c']
    erode_x, erode_y = local_d['erode_x'], local_d['erode_y']

    xi, yi = Var('xi'), Var('yi')

    if 0:
        erode_x.root().vectorize(x, 8)
        erode_y.vectorize(x, 8)
    else:
        erode_x.root()
        erode_x.split(y, y, yi, 8)
        erode_x.parallel(y)

        erode_y.root()
        erode_y.split(y, y, yi, 8)
        erode_y.parallel(y)

    test = filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo2.png'), disp_time=True)
    for i in range(5):
        test()
    test().show()

if __name__ == '__main__':
    main()

    
