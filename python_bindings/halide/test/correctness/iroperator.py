import contextlib
import io
import math
import sys

import halide as hl


# redirect_stdout() requires Python3, alas
@contextlib.contextmanager
def _redirect_stdout(out):
    old_out = sys.stdout
    sys.stdout = out
    try:
        yield out
    finally:
        sys.stdout = old_out


def test_print_expr():
    x = hl.Var("x")
    f = hl.Func("f")
    f[x] = hl.print(
        hl.cast(hl.UInt(8), x), "is what", "the", 1, "and", hl.f32(3.1415), "saw"
    )
    buf = hl.Buffer(hl.UInt(8), [1])
    output = io.StringIO()
    with _redirect_stdout(output):
        f.realize(buf)
        expected = "0 is what the 1 and 3.141500 saw\n"
        actual = output.getvalue()
        assert expected == actual, f"Expected: {expected}, Actual: {actual}"


def test_print_when():
    x = hl.Var("x")
    f = hl.Func("f")
    f[x] = hl.print_when(x == 3, hl.cast(hl.UInt(8), x * x), "is result at", x)
    buf = hl.Buffer(hl.UInt(8), [10])
    output = io.StringIO()
    with _redirect_stdout(output):
        f.realize(buf)
        expected = "9 is result at 3\n"
        actual = output.getvalue()
        assert expected == actual, f"Expected: {expected}, Actual: {actual}"


def test_select():
    x = hl.Var()
    f = hl.Func()
    # fmt: off
    f[x] = hl.select(x == 0, 31,
                     x == 2, (x * 24),
                     x == 2, 999,  # should be ignored: first condition wins
                             x)
    # fmt: on
    b = f.realize([4])
    assert b[0] == 31
    assert b[1] == 1
    assert b[2] == 48
    assert b[3] == 3


def test_select_bad_argmax():
    x = hl.Var()
    f = hl.Func()
    f[x] = hl.sin(hl.f32(math.pi) * x / 16.0)

    r = hl.RDom([(0, 10)])
    g = hl.Func()

    g[()] = (0, f.type().min())
    try:
        g[()] = hl.select(f[r] > g[()][1], (f[r], r), g[()])
    except hl.HalideError as e:
        assert (
            "Error: The second and third arguments to a select do not have a matching type:"
            in str(e)
        )

    g[()] = hl.select(f[r] > g[()][1], (r, f[r]), g[()])

    idx, val = g.realize([])
    assert idx[()] == 8
    assert val[()] == 1.0


def test_mux():
    c = hl.Var()
    f = hl.Func()
    f[c] = hl.mux(c, [123, 456, c])
    b = f.realize([4])
    assert b[0] == 123
    assert b[1] == 456
    assert b[2] == 2
    assert b[3] == 3


def test_mux_tuple():
    f = hl.Func()
    g = hl.Func()
    x = hl.Var()
    c = hl.Var()
    g[x] = (123, 456, x)
    f[x, c] = hl.mux(c, g[x])
    b = f.realize([1, 4])
    assert b[0, 0] == 123
    assert b[0, 1] == 456
    assert b[0, 2] == 0
    assert b[0, 3] == 0


def test_minmax():
    x = hl.Var()
    f = hl.Func()
    # fmt: off
    f[x] = hl.select(x == 0,              hl.min(x, 1),
                     (x == 2) | (x == 4), hl.i32(hl.min(hl.f32(x), hl.f32(3.2), x*hl.f32(2.1))),
                     x == 3,              hl.max(x, x * 3, 1, x * 4),
                                          x)
    # fmt: on
    b = f.realize([5])
    assert b[0] == 0
    assert b[1] == 1, b[1]
    assert b[2] == 2
    assert b[3] == 12
    assert b[4] == 3


def test_tuple_helpers():
    x = hl.Var("x")
    p = hl.Param(hl.Int(32), "p", 1)

    # Helpers that don't imply any math distribute across Tuples.
    f = hl.Func("f")
    f[x] = hl.select(x < 10, (0, 0), hl.likely((x, x + 1)))
    a, b = f.realize([20])
    for i in range(20):
        assert a[i] == (0 if i < 10 else i)
        assert b[i] == (0 if i < 10 else i + 1)

    # The result is an hl.Tuple, which can be indexed and iterated.
    t = hl.likely((x, x + 1))
    assert len(t) == 2
    assert len(list(t)) == 2
    f = hl.Func("f")
    f[x] = t[0] + t[1]
    a = f.realize([20])
    for i in range(20):
        assert a[i] == 2 * i + 1

    f = hl.Func("f")
    f[x] = hl.likely_if_innermost((x, x + 1))
    a, b = f.realize([20])
    for i in range(20):
        assert a[i] == i
        assert b[i] == i + 1

    f = hl.Func("f")
    f[x] = hl.strict_float((hl.f32(x) + 1.0, hl.f32(x) * 2.0))
    a, b = f.realize([20])
    for i in range(20):
        assert a[i] == i + 1
        assert b[i] == i * 2

    f = hl.Func("f")
    f[x] = hl.memoize_tag((x, x + 1), p)
    a, b = f.realize([20])
    for i in range(20):
        assert a[i] == i
        assert b[i] == i + 1

    f = hl.Func("f")
    f[x] = hl.require(p > 0, (x, x + 1), "p was", p)
    a, b = f.realize([20])
    for i in range(20):
        assert a[i] == i
        assert b[i] == i + 1

    f = hl.Func("f")
    f[x] = hl.print((x, x * 2), "at", x)
    output = io.StringIO()
    with _redirect_stdout(output):
        a, b = f.realize([3])
        expected = "0 0 at 0\n1 2 at 1\n2 4 at 2\n"
        actual = output.getvalue()
        assert expected == actual, f"Expected: {expected}, Actual: {actual}"
    for i in range(3):
        assert a[i] == i
        assert b[i] == i * 2

    f = hl.Func("f")
    f[x] = hl.print_when(x == 1, (x, x * 2), "at", x)
    output = io.StringIO()
    with _redirect_stdout(output):
        a, b = f.realize([3])
        expected = "1 2 at 1\n"
        actual = output.getvalue()
        assert expected == actual, f"Expected: {expected}, Actual: {actual}"
    for i in range(3):
        assert a[i] == i
        assert b[i] == i * 2


if __name__ == "__main__":
    test_print_expr()
    test_print_when()
    test_tuple_helpers()
    test_select()
    test_select_bad_argmax()
    test_mux()
    test_minmax()
