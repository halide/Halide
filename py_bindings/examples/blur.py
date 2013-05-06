import sys; sys.path += ['..', '.']
from halide import *

OUT_DIMS = (6400, 4864, 3)

def filter_func(dtype=UInt(16)):
    "Simple 3x3 blur."
    input = UniformImage(dtype, 2, 'input')
    x = Var('x')
    y = Var('y')
    #c = Var('c')
    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3
    
    tune_ref_schedules = {'human': \
                              "blur_y.split(y, y, yi, 8).parallel(y).vectorize(x, 8)\n" \
                              "blur_x.chunk(y, yi).vectorize(x, 8)"}
    #tune_constraints = 'blur_y.bound(c, 0, 3)'
    tune_in_images = ['apollo1.png']
    tune_out_dims = OUT_DIMS

    return (input, blur_y, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()

    x, y = local_d['x'], local_d['y'] #, local_d['c']
    blur_x, blur_y = local_d['blur_x'], local_d['blur_y']

    xi, yi = Var('xi'), Var('yi')

    schedule = 1
    
    if schedule == 0:       # Human schedule, no store-compute chunking
        blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
        blur_x.chunk(x).vectorize(x, 8)
    elif schedule == 1:
        blur_y.split(y, y, yi, 8).parallel(y).vectorize(x, 8)
        blur_x.chunk(y, yi).vectorize(x, 8)
        
    test = filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo.png'), disp_time=True, out_dims = OUT_DIMS, times=5)().show()

if __name__ == '__main__':
    main()

    
