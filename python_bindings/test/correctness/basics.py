import re

import halide as hl


def test_compiletime_error():
    x = hl.Var("x")
    y = hl.Var("y")
    f = hl.Func("f")
    f[x, y] = hl.u16(x + y)
    # Deliberate type-mismatch error
    buf = hl.Buffer(hl.UInt(8), [2, 2])
    try:
        f.realize(buf)
    except hl.HalideError as e:
        assert (
            "Output buffer f has type uint16 but type of the buffer passed in is uint8"
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"


def test_runtime_error():
    x = hl.Var("x")
    f = hl.Func("f")
    f[x] = hl.u8(x)
    f.bound(x, 0, 1)
    # Deliberate runtime error
    buf = hl.Buffer(hl.UInt(8), [10])
    try:
        f.realize(buf)
    except hl.HalideError as e:
        assert "do not cover required region" in str(e)
    else:
        assert False, "Did not see expected exception!"


def test_misused_and():
    x = hl.Var("x")
    y = hl.Var("y")
    f = hl.Func("f")
    try:
        f[x, y] = hl.print_when(x == 0 and y == 0, 0, "x=", x, "y=", y)
        f.realize([10, 10])
    except ValueError as e:
        assert "cannot be converted to a bool" in str(e)
    else:
        assert False, "Did not see expected exception!"


def test_misused_or():
    x = hl.Var("x")
    y = hl.Var("y")
    f = hl.Func("f")
    try:
        f[x, y] = hl.print_when(x == 0 or y == 0, 0, "x=", x, "y=", y)
        f.realize([10, 10])
    except ValueError as e:
        assert "cannot be converted to a bool" in str(e)
    else:
        assert False, "Did not see expected exception!"


def test_basics():
    input = hl.ImageParam(hl.UInt(16), 2, "input")
    x, y = hl.Var("x"), hl.Var("y")

    blur_x = hl.Func("blur_x")
    blur_xx = hl.Func("blur_xx")
    blur_y = hl.Func("blur_y")

    yy = hl.i32(1)
    assert yy.type() == hl.Int(32)

    z = x + 1
    input[x, y]
    input[0, 0]
    input[z, y]
    input[x + 1, y]
    input[x, y] + input[x + 1, y]

    if False:
        aa = blur_x[x, y]
        bb = blur_x[x, y + 1]
        aa + bb
        blur_x[x, y] + blur_x[x, y + 1]

    (input[x, y] + input[x + 1, y]) / 2
    blur_x[x, y]
    blur_xx[x, y] = input[x, y]

    blur_x[x, y] = (input[x, y] + input[x + 1, y] + input[x + 2, y]) / 3
    blur_y[x, y] = (blur_x[x, y] + blur_x[x, y + 1] + blur_x[x, y + 2]) / 3

    xi, yi = hl.Var("xi"), hl.Var("yi")
    blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    blur_x.compute_at(blur_y, x).vectorize(x, 8)
    blur_y.compile_jit()


def test_basics2():
    input = hl.ImageParam(hl.Float(32), 3, "input")
    hl.Param(hl.Float(32), "r_sigma", 0.1)
    s_sigma = 8

    x = hl.Var("x")
    y = hl.Var("y")
    hl.Var("z")
    hl.Var("c")

    # Add a boundary condition
    clamped = hl.Func("clamped")
    clamped[x, y] = input[
        hl.clamp(x, 0, input.width() - 1), hl.clamp(y, 0, input.height() - 1), 0
    ]

    # Construct the bilateral grid
    r = hl.RDom([(0, s_sigma), (0, s_sigma)], "r")
    clamped[x * s_sigma, y * s_sigma]
    clamped[x * s_sigma * hl.i32(1), y * s_sigma * hl.i32(1)]
    clamped[x * s_sigma - hl.i32(s_sigma // 2), y * s_sigma - hl.i32(s_sigma // 2)]
    clamped[x * s_sigma - s_sigma // 2, y * s_sigma - s_sigma // 2]
    clamped[x * s_sigma + r.x - s_sigma // 2, y * s_sigma + r.y - s_sigma // 2]

    try:
        clamped[x * s_sigma - s_sigma / 2, y * s_sigma - s_sigma / 2]
    except hl.HalideError as e:
        assert "Implicit cast from float32 to int" in str(e)
    else:
        assert False, "Did not see expected exception!"


def test_basics3():
    input = hl.ImageParam(hl.Float(32), 3, "input")
    r_sigma = hl.Param(
        hl.Float(32), "r_sigma", 0.1
    )  # Value needed if not generating an executable
    s_sigma = 8  # This is passed during code generation in the C++ version

    x = hl.Var("x")
    y = hl.Var("y")
    z = hl.Var("z")
    c = hl.Var("c")

    # Add a boundary condition
    clamped = hl.Func("clamped")
    clamped[x, y] = input[
        hl.clamp(x, 0, input.width() - 1), hl.clamp(y, 0, input.height() - 1), 0
    ]

    # Construct the bilateral grid
    r = hl.RDom([(0, s_sigma), (0, s_sigma)], "r")
    val = clamped[x * s_sigma + r.x - s_sigma // 2, y * s_sigma + r.y - s_sigma // 2]
    val = hl.clamp(val, 0.0, 1.0)
    zi = hl.i32((val / r_sigma) + 0.5)
    histogram = hl.Func("histogram")
    histogram[x, y, z, c] = 0.0

    ss = hl.select(c == 0, val, 1.0)
    left = histogram[x, y, zi, c]
    left += 5
    left += ss


def test_basics4():
    # Test for f[g[r]] = ...
    # See https://github.com/halide/Halide/issues/4285
    x = hl.Var("x")
    f = hl.Func("f")
    g = hl.Func("g")
    g[x] = 1
    f[x] = 0.0
    r = hl.RDom([(0, 100)])
    f[g[r]] = 2.5
    f.compute_root()
    f.compile_jit()


def test_basics5():
    # Test Func.in_()
    x, y = hl.Var("x"), hl.Var("y")
    f = hl.Func("f")
    g = hl.Func("g")
    h = hl.Func("h")
    f[x, y] = y
    r = hl.RDom([(0, 100)])
    g[x] = 0
    g[x] += f[x, r]
    h[x] = 0
    h[x] += f[x, r]
    f.in_(g).compute_at(g, x)
    f.in_(h).compute_at(h, x)
    g.compute_root()
    h.compute_root()
    p = hl.Pipeline([g, h])
    p.compile_jit()


def test_float_or_int():
    x = hl.Var("x")
    i32, f32 = hl.Int(32), hl.Float(32)

    assert hl.Expr(x).type() == i32
    assert (x * 2).type() == i32
    assert (x / 2).type() == i32
    assert ((x // 2) - 1 + 2 * (x % 2)).type() == i32
    assert ((x / 2) - 1 + 2 * (x % 2)).type() == i32
    assert (x / 2).type() == i32
    assert (x / 2.0).type() == f32
    assert (x // 2).type() == i32
    assert ((x // 2) - 1).type() == i32
    assert (x % 2).type() == i32
    assert (2 * (x % 2)).type() == i32
    assert ((x // 2) - 1 + 2 * (x % 2)).type() == i32

    assert type(x) is hl.Var
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
    assert (x % 2).type() == i32
    assert ((x % 2) * 2).type() == i32
    assert (2 * (x % 2)).type() == i32
    assert ((x + 2) * 2).type() == i32


def test_operator_order():
    x = hl.Var("x")
    f = hl.Func("f")
    x + 1
    1 + x
    f[x] = x**2
    f[x] + 1
    hl.Expr(1) + f[x]
    1 + f[x]


def _check_is_u16(e):
    assert e.type() == hl.UInt(16), e.type()


def test_int_promotion():
    # Verify that (Exprlike op literal) correctly matches the type
    # of the literal to the Exprlike (rather than promoting the result to int32).
    # All types that use add_binary_operators() should be tested here.

    x = hl.Var("x")
    # All the binary ops are handled the same, so + is good enough

    # Exprlike = FuncRef
    f = hl.Func("f")
    f[x] = hl.u16(x)
    _check_is_u16(f[x] + 2)
    _check_is_u16(2 + f[x])

    # Exprlike = Expr
    e = hl.Expr(f[x])
    _check_is_u16(e + 2)
    _check_is_u16(2 + e)

    # Exprlike = Param
    p = hl.Param(hl.UInt(16))
    _check_is_u16(p + 2)
    _check_is_u16(2 + p)

    # Exprlike = RDom/RVar
    # Exprlike = Var
    # (skipped, since these can never have values of any type other than int32)


def test_vector_tile():
    # Test Func.tile() and Stage.tile() with vector arguments
    x, y, z = [hl.Var(c) for c in "xyz"]
    xi, yi, zi = [hl.Var(c + "i") for c in "xyz"]
    xo, yo, zo = [hl.Var(c + "o") for c in "xyz"]
    f = hl.Func("f")
    g = hl.Func("g")
    hl.Func("h")
    f[x, y] = y
    f[x, y] += x
    g[x, y, z] = x + y
    g[x, y, z] += z
    f.tile([x, y], [xo, yo], [x, y], [8, 8])
    f.update(0).tile([x, y], [xo, yo], [xi, yi], [8, 8])
    g.tile([x, y], [xo, yo], [x, y], [8, 8], hl.TailStrategy.RoundUp)
    g.update(0).tile([x, y], [xo, yo], [xi, yi], [8, 8], hl.TailStrategy.GuardWithIf)
    p = hl.Pipeline([f, g])
    p.compile_jit()


def test_scalar_funcs():
    input = hl.ImageParam(hl.UInt(16), 0, "input")

    f = hl.Func("f")
    g = hl.Func("g")

    input[()]

    (input[()] + input[()]) / 2
    f[()]
    g[()]

    f[()] = (input[()] + input[()] + input[()]) / 3
    g[()] = (f[()] + f[()] + f[()]) / 3

    g.compile_jit()


def test_bool_conversion():
    x = hl.Var("x")
    f = hl.Func("f")
    f[x] = x
    # Verify that this doesn't fail with 'Argument passed to specialize must be of type bool'
    f.compute_root().specialize(True)


def test_typed_funcs():
    x = hl.Var("x")
    y = hl.Var("y")

    f = hl.Func("f")
    assert not f.defined()
    try:
        assert f.type() == hl.Int(32)
    except hl.HalideError as e:
        assert "it is undefined" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        assert f.outputs() == 0
    except hl.HalideError as e:
        assert "it is undefined" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        assert f.dimensions() == 0
    except hl.HalideError as e:
        assert "it is undefined" in str(e)
    else:
        assert False, "Did not see expected exception!"

    f = hl.Func(hl.Int(32), 2, "f")
    assert not f.defined()
    assert f.type() == hl.Int(32)
    assert f.types() == [hl.Int(32)]
    assert f.outputs() == 1
    assert f.dimensions() == 2

    f = hl.Func([hl.Int(32), hl.Float(64)], 3, "f")
    assert not f.defined()
    try:
        assert f.type() == hl.Int(32)
    except hl.HalideError as e:
        assert "it returns a Tuple" in str(e)
    else:
        assert False, "Did not see expected exception!"

    assert f.types() == [hl.Int(32), hl.Float(64)]
    assert f.outputs() == 2
    assert f.dimensions() == 3

    f = hl.Func(hl.Int(32), 1, "f")
    try:
        f[x, y] = hl.i32(0)
        f.realize([10, 10])
    except hl.HalideError as e:
        assert (
            "is constrained to have exactly 1 dimensions, but is defined with 2 dimensions"
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"

    f = hl.Func(hl.Int(32), 2, "f")
    try:
        f[x, y] = hl.i16(0)
        f.realize([10, 10])
    except hl.HalideError as e:
        assert (
            "is constrained to only hold values of type int32 but is defined with values of type int16"
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"

    f = hl.Func((hl.Int(32), hl.Float(32)), 2, "f")
    try:
        f[x, y] = (hl.i16(0), hl.f64(0))
        f.realize([10, 10])
    except hl.HalideError as e:
        assert (
            "is constrained to only hold values of type (int32, float32) but is defined with values of type (int16, float64)"
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"


def test_requirements():
    delta = hl.Param(hl.Int(32), "delta")
    x = hl.Var("x")
    f = hl.Func("f_requirements")
    f[x] = x + delta

    # Add a requirement
    p = hl.Pipeline([f])
    p.add_requirement(delta != 0)  # error_args omitted
    p.add_requirement(delta > 0, "negative values are bad", delta)

    delta.set(1)
    p.realize([10])

    try:
        delta.set(0)
        p.realize([10])
    except hl.HalideError as e:
        assert "Requirement Failed: (false)" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        delta.set(-1)
        p.realize([10])
    except hl.HalideError as e:
        assert "Requirement Failed: (false) negative values are bad -1" in str(e)
    else:
        assert False, "Did not see expected exception!"


def test_implicit_convert_int64():
    assert (hl.i32(0) + 0x7FFFFFFF).type() == hl.Int(32)
    assert (hl.i32(0) + (0x7FFFFFFF + 1)).type() == hl.Int(64)


def test_pow_rpow():
    two = hl.Expr(2.0)
    x = hl.Var("x")
    for three in (3, hl.Expr(3)):
        f = hl.Func("f")
        f[x] = 0.0
        f[0] = two**three
        f[1] = three**two
        assert list(f.realize([2])) == [8.0, 9.0]


def test_unevaluated_funcref():
    x = hl.Var("x")
    f = hl.Func("f")

    # `ref` is a FuncRef
    ref = f[x]

    # Because the func `f` did not have a pure definition, `ref` is set to an
    # UnevaluatedFuncRefExpr with RHS `1` and operator `+`. In C++, this would
    # immediately create a new update stage (and the associated pure definition).
    ref += 1

    try:
        # We've gone too far... UnevaluatedFuncRefExpr doesn't define any binary
        # operators. This does not implicitly cast to Expr.
        ref += 1
    except TypeError as e:
        assert (
            str(e)
            == "unsupported operand type(s) for +=: 'halide.halide_._UnevaluatedFuncRefExpr' and 'int'"
        )
    else:
        assert False, "Did not see expected exception!"

    # Since `ref` is still an unevaluated FuncRef, this line will cause the creation
    # of an update stage along with an implicit pure definition (because the operation
    # is +, the initialization will be to 0).
    f[x] = ref
    try:
        # Trying to do this twice is asking for problems, so we don't allow it.
        f[x] = ref
    except hl.HalideError as e:
        assert re.match(
            r"Cannot use an unevaluated reference to 'f(\$\d+)?' to define an update when a pure definition already exists.",
            str(e),
        )
    else:
        assert False, "Did not see expected exception!"

    # Check that defining the first update stage worked.
    assert f.realize([1])[0] == 1

    try:
        # We also can't use the unevaluated ref to define an update in a
        # different func (even if they look like they have the same name)
        f = hl.Func("f")
        f[x] = ref
    except hl.HalideError as e:
        assert re.match(
            r"Cannot use an unevaluated reference to 'f\$\d+' to define an update in a different func 'f\$\d+'.",
            str(e),
        )
    else:
        assert False, "Did not see expected exception!"

    # A few more tests that check the results of various equivalent-looking rewrites.
    f = hl.Func("f")
    ref = f[x]
    ref += 1
    f[x] = ref
    assert f.realize([1])[0] == 1

    f = hl.Func("f")
    ref = f[x] + 1
    f[x] = ref
    assert f.realize([1])[0] == 1

    f = hl.Func("f")
    f[x] = f[x] + 1
    assert f.realize([1])[0] == 1

    f = hl.Func("f")
    f[x] += 1
    assert f.realize([1])[0] == 1

    try:
        f = hl.Func("f")
        # This is invalid because we only allow unevaluated func refs on the LHS of a
        # binary operator.
        f[x] = 1 + f[x]
    except hl.HalideError as e:
        assert re.match(
            r"Error: Can't call Func \"f(\$\d+)?\" because it has not yet been defined\.",
            str(e),
        )
    else:
        assert False, "Did not see expected exception!"

    try:
        # This is OK
        g = hl.Func("g")
        g[0] += 1
        assert list(g.realize([2])) == [1, 0]

        # This is invalid because the list of arguments changed
        f = hl.Func("f")
        f[0] = f[x] + 1
    except hl.HalideError as e:
        assert re.match(
            r"Cannot use an unevaluated reference to 'f(\$\d+)?' to define an update with different arguments\.",
            str(e),
        )
    else:
        assert False, "Did not see expected exception!"


def test_implicit_update_by_int():
    x = hl.Var("x")

    f = hl.Func("f")
    f[x] += 1
    assert f.realize([1])[0] == 1

    f = hl.Func("f")
    f[x] -= 1
    assert f.realize([1])[0] == -1

    f = hl.Func("f")
    f[x] *= 2
    assert f.realize([1])[0] == 2

    f = hl.Func("f")
    f[x] /= 1
    assert f.realize([1])[0] == 1

    f = hl.Func("f")
    f[x] /= 2
    assert f.realize([1])[0] == 0


def test_implicit_update_by_float():
    # The floating-point equality comparisons in this test
    # should be exact in any halfway sane floating point
    # implementation. If these checks fail because of
    # imprecision, we should be informed.

    x = hl.Var("x")

    f = hl.Func("f")
    f[x] += 1.0
    assert f.realize([1])[0] == 1.0

    f = hl.Func("f")
    f[x] -= 1.0
    assert f.realize([1])[0] == -1.0

    f = hl.Func("f")
    f[x] *= 2.0
    assert f.realize([1])[0] == 2.0

    f = hl.Func("f")
    f[x] /= 1.0
    assert f.realize([1])[0] == 1.0

    f = hl.Func("f")
    f[x] /= 2.0
    assert f.realize([1])[0] == 0.5


if __name__ == "__main__":
    test_compiletime_error()
    test_runtime_error()
    test_misused_and()
    test_misused_or()
    test_typed_funcs()
    test_float_or_int()
    test_operator_order()
    test_int_promotion()
    test_vector_tile()
    test_basics()
    test_basics2()
    test_basics3()
    test_basics4()
    test_basics5()
    test_scalar_funcs()
    test_bool_conversion()
    test_requirements()
    test_implicit_convert_int64()
    test_pow_rpow()
    test_unevaluated_funcref()
    test_implicit_update_by_int()
    test_implicit_update_by_float()
