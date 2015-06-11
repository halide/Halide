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


def get_blur(input):

    assert type(input) == ImageParam
    assert input.dimensions() == 2

    x, y = Var('x'), Var('y')

    #clamped_input = repeat_edge(cast(UInt(16), input))
    #ci = clamped_input
    clamped_input = repeat_edge(input)

    input_uint16 = Func('input_uint16')
    input_uint16[x,y] = cast(UInt(16), clamped_input[x,y])
    ci = input_uint16 # cast does not work over ImageParam ?

    blur_x = Func('blur_x')
    blur_y = Func('blur_y')

    blur_x[x,y] = (ci[x,y]+ci[x+1,y]+ci[x+2,y])/3
    blur_y[x,y] = cast(UInt(8), (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3)

    # schedule
    xi, yi = Var('xi'), Var('yi')
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)

    return blur_y

def main():
    input = ImageParam(UInt(8), 2, "input_param")
    blur = get_blur(input)
    blur.compile_jit()

    import numpy as np
    from scipy.misc import imread, imsave
    import os.path
    image_path = os.path.join(os.path.dirname(__file__), "../../apps/images/rgb.png")
    assert os.path.exists(image_path), \
        "Could not find %s" % image_path
    rgb_data = imread(image_path)
    print("rgb_data", type(rgb_data), rgb_data.shape, rgb_data.dtype)

    grey_data = np.mean(rgb_data, axis=2, dtype=float).astype(rgb_data.dtype)
    input_data = np.copy(grey_data, order="F")
    input_image = ndarray_to_image(input_data, "input_image")
    input.set(input_image)

    output_data = np.empty(rgb_data.shape[:2],
                              dtype=rgb_data.dtype, order="F")
    output_image = ndarray_to_image(output_data, "output_image")

    print("input_image", input_image)
    print("output_image", output_image)

    blur.realize(output_image)

    output_path = "blur_result.png"
    print("blur realized on output image.",
          "Result saved at", output_path)

    imsave("input_data.png", grey_data)
    imsave(output_path, output_data)

    return

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

