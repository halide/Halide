"""
Erode application using Python Halide bindings
"""

from halide import *

def main(dtype=UInt(16)):
    "Erode on 5x5 stencil, first erode x then erode y."
    input = ImageParam(dtype, 3, 'input')
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
    
    yi = Var('yi')
    
    # CPU Schedule    
    erode_x.compute_root().split(y, y, yi, 8).parallel(y)
    erode_y.compute_root().split(y, y, yi, 8).parallel(y)
        
    eval_func = filter_image(input, erode_y, builtin_image(), disp_time=True, times=5)
    eval_func().show()

if __name__ == '__main__':
    main()

