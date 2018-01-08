from __future__ import print_function

from contextlib import contextmanager
import halide as hl
import sys

try:
    from StringIO import StringIO  # Python2
except ImportError:
    from io import StringIO        # Python3

# redirect_stdout() requires Python3, alas
@contextmanager
def _redirect_stdout(out):
    old_out = sys.stdout
    sys.stdout = out
    try:
        yield out
    finally:
        sys.stdout = old_out

def test_compiletime_error():

    x = hl.Var('x')
    y = hl.Var('y')
    f = hl.Func('f')
    f[x, y] = hl.cast(hl.UInt(16), x + y)
    # Deliberate type-mismatch error
    buf = hl.Buffer(hl.UInt(8), 2, 2)
    try:
        f.realize(buf)
    except RuntimeError as e:
        print('Saw expected exception (%s)' % str(e))
    else:
        assert False, 'Did not see expected exception!'

def test_runtime_error():

    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.cast(hl.UInt(8), x)
    f.bound(x, 0, 1)
    # Deliberate runtime error
    buf = hl.Buffer(hl.UInt(8), 10)
    try:
        f.realize(buf)
    except RuntimeError as e:
        print('Saw expected exception (%s)' % str(e))
    else:
        assert False, 'Did not see expected exception!'

    return

def test_print_expr():
    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.print(hl.cast(hl.UInt(8), x), 'is what', 'the', 1, 'and', 3.1415, 'saw')
    buf = hl.Buffer(hl.UInt(8), 1)
    output = StringIO()
    with _redirect_stdout(output):
        f.realize(buf)
        expected = '0 is what the 1 and 3.141500 saw\n'
        actual = output.getvalue()
        assert expected == actual, "Expected: %s, Actual: %s" % (expected, actual)

    return

def test_print_when():

    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.print_when(x == 3, hl.cast(hl.UInt(8), x*x), 'is result at', x)
    buf = hl.Buffer(hl.UInt(8), 10)
    output = StringIO()
    with _redirect_stdout(output):
        f.realize(buf)
        expected = '9 is result at 3\n'
        actual = output.getvalue()
        assert expected == actual, "Expected: %s, Actual: %s" % (expected, actual)

    return

def test_types():

    t0 = hl.Int(32)
    t1 = hl.Int(16)

    assert t0 != t1
    assert t0.is_float() == False
    assert t1.is_float() == False

    print("hl.Int(32) type:", hl.Int(32))
    print("hl.Int(16) type:", hl.Int(16))

    return

def test_basics():

    input = hl.ImageParam(hl.UInt(16), 2, 'input')
    x, y = hl.Var('x'), hl.Var('y')

    blur_x = hl.Func('blur_x')
    blur_xx = hl.Func('blur_xx')
    blur_y = hl.Func('blur_y')

    yy = hl.cast(hl.Int(32), 1)
    assert yy.type() == hl.Int(32)
    print("yy type:", yy.type())

    z = x + 1
    input[x,y]
    input[0,0]
    input[z,y]
    input[x+1,y]
    print("ping 0.2")
    input[x,y]+input[x+1,y]

    if False:
        aa = blur_x[x,y]
        bb = blur_x[x,y+1]
        aa + bb
        blur_x[x,y]+blur_x[x,y+1]

    print("ping 0.3")
    (input[x,y]+input[x+1,y]) / 2
    print("ping 0.4")
    blur_x[x,y]
    print("ping 0.4.1")
    blur_xx[x,y] = input[x,y]



    print("ping 0.5")
    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    print("ping 1")
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

    xi, yi = hl.Var('xi'), hl.Var('yi')
    print("ping 2")
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)


    blur_y.compile_jit()
    print("Compiled to jit")

    return

def test_basics2():

    input = hl.ImageParam(hl.Float(32), 3, 'input')
    r_sigma = hl.Param(hl.Float(32), 'r_sigma', 0.1) # Value needed if not generating an executable
    s_sigma = 8 # This is passed during code generation in the C++ version

    x = hl.Var('x')
    y = hl.Var('y')
    z = hl.Var('z')
    c = hl.Var('c')

    # Add a boundary condition
    clamped = hl.Func('clamped')
    clamped[x, y] = input[hl.clamp(x, 0, input.width()-1),
                          hl.clamp(y, 0, input.height()-1),0]

    # Construct the bilateral grid
    r = hl.RDom(0, s_sigma, 0, s_sigma, 'r')
    val0 = clamped[x * s_sigma, y * s_sigma]
    val00 = clamped[x * s_sigma * hl.cast(hl.Int(32), 1), y * s_sigma * hl.cast(hl.Int(32), 1)]
    #val1 = clamped[x * s_sigma - s_sigma/2, y * s_sigma - s_sigma/2] # should fail
    val22 = clamped[x * s_sigma - hl.cast(hl.Int(32), s_sigma//2),
                    y * s_sigma - hl.cast(hl.Int(32), s_sigma//2)]
    val2 = clamped[x * s_sigma - s_sigma//2, y * s_sigma - s_sigma//2]
    val3 = clamped[x * s_sigma + r.x - s_sigma//2, y * s_sigma + r.y - s_sigma//2]

    return


def test_basics3():

    input = hl.ImageParam(hl.Float(32), 3, 'input')
    r_sigma = hl.Param(hl.Float(32), 'r_sigma', 0.1) # Value needed if not generating an executable
    s_sigma = 8 # This is passed during code generation in the C++ version

    x = hl.Var('x')
    y = hl.Var('y')
    z = hl.Var('z')
    c = hl.Var('c')

    # Add a boundary condition
    clamped = hl.Func('clamped')
    clamped[x, y] = input[hl.clamp(x, 0, input.width()-1),
                          hl.clamp(y, 0, input.height()-1),0]

    # Construct the bilateral grid
    r = hl.RDom(0, s_sigma, 0, s_sigma, 'r')
    val = clamped[x * s_sigma + r.x - s_sigma//2, y * s_sigma + r.y - s_sigma//2]
    val = hl.clamp(val, 0.0, 1.0)
    #zi = hl.cast(hl.Int(32), val * (1.0/r_sigma) + 0.5)
    zi = hl.cast(hl.Int(32), (val / r_sigma) + 0.5)
    histogram = hl.Func('histogram')
    histogram[x, y, z, c] = 0.0

    ss = hl.select(c == 0, val, 1.0)
    print("hl.select(c == 0, val, 1.0)", ss)
    left = histogram[x, y, zi, c]
    print("histogram[x, y, zi, c]", histogram[x, y, zi, c])
    print("histogram[x, y, zi, c]", left)
    left += 5
    print("histogram[x, y, zi, c] after += 5", left)
    left += ss

    return


def test_float_or_int():

    x = hl.Var('x')
    i, f =  hl.Int(32), hl.Float(32)

    assert ((x//2) - 1 + 2*(x%2)).type() == i
    assert ((x/2) - 1 + 2*(x%2)).type() == i
    assert ((x/2)).type() == i
    assert ((x/2.0)).type() == f
    assert ((x//2)).type() == i
    assert ((x//2) - 1).type() == i
    assert ((x%2)).type() == i
    assert (2*(x%2)).type() == i
    assert ((x//2) - 1 + 2*(x%2)).type() == i

    assert type(x) == hl.Var
    assert (x.as_expr()).type() == i
    assert (hl.Expr(2.0)).type() == f
    assert (hl.Expr(2)).type() == i
    assert (x + 2).type() == i
    assert (2 + x).type() == i
    assert (hl.Expr(2) + hl.Expr(3)).type() == i
    assert (hl.Expr(2.0) + hl.Expr(3)).type() == f
    assert (hl.Expr(2) + 3.0).type() == f
    assert (hl.Expr(2) + 3).type() == i
    assert (x.as_expr() + 2).type() == i # yes this failed at some point
    assert (2 + x.as_expr()).type() == i
    assert (2 * (x + 2)).type() == i # yes this failed at some point
    assert (x + 0).type() == i
    assert (x % 2).type() == i
    assert (2 * x).type() == i
    assert (x * 2).type() == i
    assert (x * 2).type() == i
    assert ((x % 2)).type() == i
    assert ((x % 2) * 2).type() == i
    #assert (2 * (x % 2)).type() == i # yes this failed at some point
    assert ((x + 2) * 2).type() == i

    return


def test_operator_order():

    x = hl.Var('x')
    f = hl.Func('f')
    x + 1
    1 + x
    print("x", x, ", x + 1", x + 1, ", 1 + x", 1 + x)
    f[x] = x ** 2
    f[x] + 1
    hl.Expr(1) + f[x]
    1 + f[x]

    return

def test_ndarray_to_image():

    if "ndarray_to_image" not in globals():
        print("Skipping test_ndarray_to_image")
        return

    import numpy

    a0 = numpy.ones((200, 300), dtype=numpy.float32)
    i0 = ndarray_to_image(a0, "float32_test_image")
    print("i0", i0)

    a1 = numpy.ones((640, 480), dtype=numpy.uint8)
    i1 = ndarray_to_image(a1, "uint8_test_image")
    print("i1", i1)


    return


def test_image_to_ndarray():

    if "image_to_ndarray" not in globals():
        print("Skipping test_image_to_ndarray")
        return

    import numpy

    i0 = Image(hl.Float(32), 50, 50)
    assert i0.type() == hl.Float(32)

    a0 = image_to_ndarray(i0)
    print("a0.shape", a0.shape)
    print("a0.dtype", a0.dtype)
    assert a0.dtype == numpy.float32

    i1 = Image(hl.Int(16), 50, 50)
    assert i1.type() == hl.Int(16)
    i1[24, 24] = 42
    assert i1(24, 24) == 42

    a1 = image_to_ndarray(i1)
    print("a1.shape", a1.shape)
    print("a1.dtype", a1.dtype)
    assert a1.dtype == numpy.int16
    assert a1[24, 24] == 42

    return

def test_param_bug():
    "see https://github.com/rodrigob/Halide/issues/1"

    p1 = hl.Param(hl.UInt(8), "p1", 0)
    p2 = hl.Param(hl.UInt(8), "p2")
    p3 = hl.Param(hl.UInt(8), 42)

    return

def test_imageparam_bug():
    "see https://github.com/rodrigob/Halide/issues/2"

    x = hl.Var("x")
    y = hl.Var("y")
    fx = hl.Func("fx")
    input = hl.ImageParam(hl.UInt(8), 1, "input")
    fx[x, y] = input[y]

    return

if __name__ == "__main__":

    test_compiletime_error()
    test_runtime_error()
    test_print_expr()
    test_print_when()
    test_imageparam_bug()
    test_param_bug()
    test_float_or_int()
    test_ndarray_to_image()
    test_image_to_ndarray()
    test_types()
    test_operator_order()
    test_basics()
    test_basics2()
    test_basics3()
