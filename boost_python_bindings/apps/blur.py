import os, sys
from halide import *

OUT_DIMS = (1536, 2560)


def filter_image(input, out_func, in_image, disp_time=False, compile=True, eval_func=None, out_dims=None, times=1): #, pad_multiple=1):
    """
    Utility function to filter an image filename or numpy array with a Halide Func, returning output Image of the same size.

    Given input and output Funcs, and filename/numpy/PIL array (in_image), returns evaluate. Calling evaluate() returns the output Image.
    """
#    print('filter_image, input=', input, 'dtype', input.type())
    dtype = input.type()
    if isinstance(input, ImageParamType):
        if isinstance(in_image, str):
            input_png = Image(dtype, in_image)
        elif isinstance(in_image, numpy.ndarray):
            input_png = Image(in_image)
        elif hasattr(in_image, 'putpixel'):         # PIL image
            input_png = Image(dtype, in_image)
        else:
            input_png = in_image
        input.set(input_png)
    else:
        input_png = input

    w = input_png.width() if out_dims is None else out_dims[0]
    h = input_png.height() if out_dims is None else out_dims[1]
    nchan = input_png.channels() if out_dims is None else (out_dims[2] if len(out_dims) >= 3 else 1)
    #print(w, h, nchan, out_dims)
    if compile:
        out_func.compile_jit()

    def evaluate():
        T = []
        for i in range(times):
            T0 = time.time()
            if eval_func is not None:
                realized = eval_func(input_png)
            else:
                if nchan != 1:
                    realized = out_func.realize(w, h, nchan)
                else:
                    realized = out_func.realize(w, h)
            T.append(time.time()-T0)
        out = Image(realized) #.set(realized)


        assert out.width() == w and out.height() == h, (out.width(), out.height(), w, h)
        if disp_time:
            if times > 1:
                print('Filtered %d times, min: %.6f secs, mean: %.6f secs.' % (times, numpy.min(T), numpy.mean(T)))
            else:
                print('Filtered in %.6f secs' % T[0])

        return out
    return evaluate


def main():
    input = ImageParam(UInt(16), 2, 'input')
    x, y = Var('x'), Var('y')

    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    print("ping 0.0")
    input[x,y]
    print("ping 0.1")
    input[x+1,y]
    print("ping 0.2")
    (input[x,y]+input[x+1,y])
    print("ping 0.3")
    (input[x,y]+input[x+1,y]) / 2
    print("ping 0.4")
    blur_x[x,y] = input[x,y]

    print("ping 0.5")
    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    print("ping 1")
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

    xi, yi = Var('xi'), Var('yi')
    print("ping 2")
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)

    print("ping 3")
    maxval = 255
    in_image = Image(UInt(16), builtin_image('rgb.png'), scale=1.0) # Set scale to 1 so that we only use 0...255 of the UInt(16) range
    print("ping 4")
    eval_func = filter_image(input, blur_y, in_image, disp_time=True, out_dims = (OUT_DIMS[0]-8, OUT_DIMS[1]-8), times=5)
    I = eval_func()
    if len(sys.argv) >= 2:
        I.save(sys.argv[1], maxval)
    else:
        I.show(maxval)

if __name__ == '__main__':
    main()

