from __future__ import print_function
from __future__ import division

from contextlib import contextmanager
import halide as hl
import numpy as np
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
    buf = hl.Buffer(hl.UInt(8), [2, 2])
    try:
        f.realize(buf)
    except RuntimeError as e:
        assert 'Buffer has type uint8, but Func "f" has type uint16.' in str(e)
    else:
        assert False, 'Did not see expected exception!'

def test_runtime_error():
    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.cast(hl.UInt(8), x)
    f.bound(x, 0, 1)
    # Deliberate runtime error
    buf = hl.Buffer(hl.UInt(8), [10])
    try:
        f.realize(buf)
    except RuntimeError as e:
        assert 'do not cover required region' in str(e)
    else:
        assert False, 'Did not see expected exception!'

def test_print_expr():
    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.print(hl.cast(hl.UInt(8), x), 'is what', 'the', 1, 'and', 3.1415, 'saw')
    buf = hl.Buffer(hl.UInt(8), [1])
    output = StringIO()
    with _redirect_stdout(output):
        f.realize(buf)
        expected = '0 is what the 1 and 3.141500 saw\n'
        actual = output.getvalue()
        assert expected == actual, "Expected: %s, Actual: %s" % (expected, actual)

def test_print_when():
    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.print_when(x == 3, hl.cast(hl.UInt(8), x*x), 'is result at', x)
    buf = hl.Buffer(hl.UInt(8), [10])
    output = StringIO()
    with _redirect_stdout(output):
        f.realize(buf)
        expected = '9 is result at 3\n'
        actual = output.getvalue()
        assert expected == actual, "Expected: %s, Actual: %s" % (expected, actual)

def test_basics():
    input = hl.ImageParam(hl.UInt(16), 2, 'input')
    x, y = hl.Var('x'), hl.Var('y')

    blur_x = hl.Func('blur_x')
    blur_xx = hl.Func('blur_xx')
    blur_y = hl.Func('blur_y')

    yy = hl.cast(hl.Int(32), 1)
    assert yy.type() == hl.Int(32)

    z = x + 1
    input[x,y]
    input[0,0]
    input[z,y]
    input[x+1,y]
    input[x,y]+input[x+1,y]

    if False:
        aa = blur_x[x,y]
        bb = blur_x[x,y+1]
        aa + bb
        blur_x[x,y]+blur_x[x,y+1]

    (input[x,y]+input[x+1,y]) / 2
    blur_x[x,y]
    blur_xx[x,y] = input[x,y]

    blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
    blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

    xi, yi = hl.Var('xi'), hl.Var('yi')
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)
    blur_y.compile_jit()


def test_basics2():
    input = hl.ImageParam(hl.Float(32), 3, 'input')
    r_sigma = hl.Param(hl.Float(32), 'r_sigma', 0.1)
    s_sigma = 8

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
    val22 = clamped[x * s_sigma - hl.cast(hl.Int(32), s_sigma//2),
                    y * s_sigma - hl.cast(hl.Int(32), s_sigma//2)]
    val2 = clamped[x * s_sigma - s_sigma//2, y * s_sigma - s_sigma//2]
    val3 = clamped[x * s_sigma + r.x - s_sigma//2, y * s_sigma + r.y - s_sigma//2]

    try:
        val1 = clamped[x * s_sigma - s_sigma/2, y * s_sigma - s_sigma/2]
    except RuntimeError as e:
        assert 'Implicit cast from float32 to int' in str(e)
    else:
        assert False, 'Did not see expected exception!'


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
    zi = hl.cast(hl.Int(32), (val / r_sigma) + 0.5)
    histogram = hl.Func('histogram')
    histogram[x, y, z, c] = 0.0

    ss = hl.select(c == 0, val, 1.0)
    left = histogram[x, y, zi, c]
    left += 5
    left += ss


def test_float_or_int():
    x = hl.Var('x')
    i32, f32 =  hl.Int(32), hl.Float(32)

    assert hl.Expr(x).type() == i32
    assert (x*2).type() == i32
    assert (x/2).type() == i32
    assert ((x//2) - 1 + 2*(x%2)).type() == i32
    assert ((x/2) - 1 + 2*(x%2)).type() == i32
    assert ((x/2)).type() == i32
    assert ((x/2.0)).type() == f32
    assert ((x//2)).type() == i32
    assert ((x//2) - 1).type() == i32
    assert ((x%2)).type() == i32
    assert (2*(x%2)).type() == i32
    assert ((x//2) - 1 + 2*(x%2)).type() == i32

    assert type(x) == hl.Var
    assert (hl.Expr(x)).type() == i32
    assert (hl.Expr(2.0)).type() == f32
    assert (hl.Expr(2)).type() == i32
    assert (x + 2).type() == i32
    assert (2 + x).type() == i32
    assert (hl.Expr(2) + hl.Expr(3)).type() == i32
    assert (hl.Expr(2.0) + hl.Expr(3)).type() == f32
    assert (hl.Expr(2) + 3.0).type() == f32
    assert (hl.Expr(2) + 3).type() == i32
    assert (hl.Expr(x) + 2).type() == i32
    assert (2 + hl.Expr(x)).type() == i32
    assert (2 * (x + 2)).type() == i32
    assert (x + 0).type() == i32
    assert (x % 2).type() == i32
    assert (2 * x).type() == i32
    assert (x * 2).type() == i32
    assert (x * 2).type() == i32
    assert ((x % 2)).type() == i32
    assert ((x % 2) * 2).type() == i32
    assert (2 * (x % 2)).type() == i32
    assert ((x + 2) * 2).type() == i32


def test_operator_order():
    x = hl.Var('x')
    f = hl.Func('f')
    x + 1
    1 + x
    f[x] = x ** 2
    f[x] + 1
    hl.Expr(1) + f[x]
    1 + f[x]

def test_ndarray_to_buffer():
    a0 = np.ones((200, 300), dtype=np.int32)

    # Buffer always shares data (when possible) by default,
    # and maintains the shape of the data source. (note that
    # the ndarray is col-major by default!)
    b0 = hl.Buffer(a0, "float32_test_buffer")
    assert b0.type() == hl.Int(32)
    assert b0.name() == "float32_test_buffer"
    assert b0.all_equal(1)

    assert b0.dim(0).min() == 0
    assert b0.dim(0).max() == 199
    assert b0.dim(0).extent() == 200
    assert b0.dim(0).stride() == 300

    assert b0.dim(1).min() == 0
    assert b0.dim(1).max() == 299
    assert b0.dim(1).extent() == 300
    assert b0.dim(1).stride() == 1

    a0[12, 34] = 56
    assert b0[12, 34] == 56

    b0[56, 34] = 12
    assert a0[56, 34] == 12


def test_buffer_to_ndarray():
    buf = hl.Buffer(hl.Int(16), [4, 4])
    assert buf.type() == hl.Int(16)
    buf.fill(0)
    buf[1, 2] = 42
    assert buf[1, 2] == 42

    # Should share storage with buf
    array_shared = np.array(buf, copy = False)
    assert array_shared.shape == (4, 4)
    assert array_shared.dtype == np.int16
    assert array_shared[1, 2] == 42

    # Should *not* share storage with buf
    array_copied = np.array(buf, copy = True)
    assert array_copied.shape == (4, 4)
    assert array_copied.dtype == np.int16
    assert array_copied[1, 2] == 42

    buf[1, 2] = 3
    assert array_shared[1, 2] == 3
    assert array_copied[1, 2] == 42

    # Ensure that Buffers that have nonzero mins get converted correctly,
    # since the Python Buffer Protocol doesn't have the 'min' concept
    cropped = buf.cropped(dimension = 0, min = 1, extent = 2)

    # Should share storage with cropped (and buf)
    cropped_array_shared = np.array(cropped, copy = False)
    assert cropped_array_shared.shape == (2, 4)
    assert cropped_array_shared.dtype == np.int16
    assert cropped_array_shared[0, 2] == 3

    # Should *not* share storage with anything
    cropped_array_copied = np.array(cropped, copy = True)
    assert cropped_array_copied.shape == (2, 4)
    assert cropped_array_copied.dtype == np.int16
    assert cropped_array_copied[0, 2] == 3

    cropped[1, 2] = 5

    assert buf[1, 2] == 5
    assert array_shared[1, 2] == 5
    assert array_copied[1, 2] == 42

    assert cropped[1, 2] == 5
    assert cropped_array_shared[0, 2] == 5
    assert cropped_array_copied[0, 2] == 3


def _assert_fn(e):
    assert e

def test_for_each_element():
    buf = hl.Buffer(hl.Float(32), [3, 4])
    for x in range(3):
        for y in range(4):
            buf[x, y] = x + y
    # Can't use 'assert' in a lambda, but can call a fn that uses it.
    buf.for_each_element(lambda pos, buf=buf: _assert_fn(buf[pos[0], pos[1]] == pos[0] + pos[1]))

def test_fill_all_equal():
    buf = hl.Buffer(hl.Int(32), [3, 4])
    buf.fill(3)
    assert buf.all_equal(3)
    buf[1, 2] = 4
    assert not buf.all_equal(3)

if __name__ == "__main__":
    test_compiletime_error()
    test_runtime_error()
    test_print_expr()
    test_print_when()
    test_float_or_int()
    test_operator_order()
    test_basics()
    test_basics2()
    test_basics3()
    test_ndarray_to_buffer()
    test_buffer_to_ndarray()
    test_for_each_element()
    test_fill_all_equal()
