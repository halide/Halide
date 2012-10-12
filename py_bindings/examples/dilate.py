import sys; sys.path += ['..', '.']
from halide import *

def filter_func(dtype=UInt(16)):
    "Dilate on 3x3 stencil."
    input = UniformImage(dtype, 3, 'input')
    x = Var('x')
    y = Var('y')
    c = Var('c')
    input_clamped = Func('input_clamped')
    dilate = Func('dilate')

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    subexp = input_clamped[x,y,c]  # TODO Debug buggy
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            if dx != 0 or dy != 0:
                subexp = max(subexp, input_clamped[x+dx,y+dy,c])
    dilate[x,y,c] = subexp #min(min(input_clamped[x-1,y-1,c],input_clamped[x,y-1,c]),input_clamped[x+1,y-1,c])

    return (input, dilate, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    
    x, y, c = local_d['x'], local_d['y'], local_d['c']
    dilate = local_d['dilate']

    xi, yi = Var('xi'), Var('yi')

    schedule = 0
    
    if schedule == 0:           # Autotuned schedule
        dilate.root().split(y, y, yi, 8).parallel(y).vectorize(x, 8)
    elif schedule == 1:         # Hand-tuned schedule (Jonathan)
        dilate.root().tile(x,y,xi,yi,8,8).vectorize(xi,8).parallel(y)
    else:
        raise ValueError
    test = filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo3.png'), disp_time=True)
    for i in range(5):
        test()
    test().show()

if __name__ == '__main__':
    main()

    
