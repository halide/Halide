import sys; sys.path += ['..', '.']
import os; os.environ['HL_DISABLE_BOUNDS_CHECKING'] = '1'
from halide import *

OUT_DIMS = (1536, 2560)

def main():
    input = ImageParam(UInt(16), 2, 'input')
    x, y = Var('x'), Var('y')

    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

    xi, yi = Var('xi'), Var('yi')

    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
#    blur_x.compute_at(blur_y, x).vectorize(x, 8)                   # This fails when Func::operator = (const Func &) is defined
    
    maxval = 255
    in_image = Image(UInt(16), '../../apps/images/rgb.png', maxval)
    eval_func = filter_image(input, blur_y, in_image, disp_time=True, out_dims = OUT_DIMS, times=5)
    eval_func().show(maxval)

if __name__ == '__main__':
    main()

