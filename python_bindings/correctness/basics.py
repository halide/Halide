
import halide as hl
import numpy as np

def test_compiletime_error():
    x = hl.Var('x')
    y = hl.Var('y')
    f = hl.Func('f')
    f[x, y] = hl.u16(x + y)
    # Deliberate type-mismatch error
    buf = hl.Buffer(hl.UInt(8), [2, 2])
    try:
        f.realize(buf)
    except RuntimeError as e:
        assert 'Output buffer f has type uint16 but type of the buffer passed in is uint8' in str(e)
    else:
        assert False, 'Did not see expected exception!'

def test_runtime_error():
    x = hl.Var('x')
    f = hl.Func('f')
    f[x] = hl.u8(x)
    f.bound(x, 0, 1)
    # Deliberate runtime error
    buf = hl.Buffer(hl.UInt(8), [10])
    try:
        f.realize(buf)
    except RuntimeError as e:
        assert 'do not cover required region' in str(e)
    else:
        assert False, 'Did not see expected exception!'

def test_misused_and():
    x = hl.Var('x')
    y = hl.Var('y')
    f = hl.Func('f')
    try:
        f[x, y] = hl.print_when(x == 0 and y == 0, 0, "x=",x, "y=", y)
        f.realize(10, 10)
    except ValueError as e:
        assert 'cannot be converted to a bool' in str(e)
    else:
        assert False, 'Did not see expected exception!'

def test_misused_or():
    x = hl.Var('x')
    y = hl.Var('y')
    f = hl.Func('f')
    try:
        f[x, y] = hl.print_when(x == 0 or y == 0, 0, "x=",x, "y=", y)
        f.realize(10, 10)
    except ValueError as e:
        assert 'cannot be converted to a bool' in str(e)
    else:
        assert False, 'Did not see expected exception!'

def test_basics():
    input = hl.ImageParam(hl.UInt(16), 2, 'input')
    x, y = hl.Var('x'), hl.Var('y')

    blur_x = hl.Func('blur_x')
    blur_xx = hl.Func('blur_xx')
    blur_y = hl.Func('blur_y')

    yy = hl.i32(1)
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
    r = hl.RDom([(0, s_sigma), (0, s_sigma)], 'r')
    val0 = clamped[x * s_sigma, y * s_sigma]
    val00 = clamped[x * s_sigma * hl.i32(1), y * s_sigma * hl.i32(1)]
    val22 = clamped[x * s_sigma - hl.i32(s_sigma//2),
                    y * s_sigma - hl.i32(s_sigma//2)]
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
    r = hl.RDom([(0, s_sigma), (0, s_sigma)], 'r')
    val = clamped[x * s_sigma + r.x - s_sigma//2, y * s_sigma + r.y - s_sigma//2]
    val = hl.clamp(val, 0.0, 1.0)
    zi = hl.i32((val / r_sigma) + 0.5)
    histogram = hl.Func('histogram')
    histogram[x, y, z, c] = 0.0

    ss = hl.select(c == 0, val, 1.0)
    left = histogram[x, y, zi, c]
    left += 5
    left += ss

def test_basics4():
    # Test for f[g[r]] = ...
    # See https://github.com/halide/Halide/issues/4285
    x = hl.Var('x')
    f = hl.Func('f')
    g = hl.Func('g')
    g[x] = 1
    f[x] = 0.0
    r = hl.RDom([(0, 100)])
    f[g[r]] = 2.5
    f.compute_root()
    f.compile_jit()

def test_basics5():
    x, y = hl.Var('x'), hl.Var('y')
    f = hl.Func('f')
    g = hl.Func('g')
    h = hl.Func('h')
    f[x, y] = y
    r = hl.RDom([(0, 100)])
    g[x] = 0
    g[x] += f[x, r]
    h[x] = 0
    h[x] += f[x, r]
    f.inside(g).compute_at(g, x)
    f.inside(h).compute_at(h, x)
    g.compute_root()
    h.compute_root()
    p = hl.Pipeline([g, h])
    p.compile_jit()

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

if __name__ == "__main__":
    test_compiletime_error()
    test_runtime_error()
    test_misused_and()
    test_misused_or()
    test_float_or_int()
    test_operator_order()
    test_basics()
    test_basics2()
    test_basics3()
    test_basics4()
    test_basics5()
