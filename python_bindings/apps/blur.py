import sys; sys.path += ['..', '.']
import os; os.environ['HL_DISABLE_BOUNDS_CHECKING'] = '1'
from halide import *

OUT_DIMS = (1536, 2560)

def filter_func(dtype=UInt(16)):
    "Simple 3x3 blur."
    input = ImageParam(dtype, 2, 'input')
    x = Var('x')
    y = Var('y')
    #c = Var('c')
    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

    return (input, blur_y, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()

    x, y = local_d['x'], local_d['y'] #, local_d['c']
    blur_x, blur_y = local_d['blur_x'], local_d['blur_y']

    xi, yi = Var('xi'), Var('yi')

    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)

    maxval = 255
    in_image = Image(UInt(16), '../../apps/images/rgb.png', maxval)
    eval_func = filter_image(input, out_func, in_image, disp_time=True, out_dims = OUT_DIMS, times=5)
    eval_func().show(maxval)

if __name__ == '__main__':
    main()

