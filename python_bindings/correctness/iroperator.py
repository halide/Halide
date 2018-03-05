from __future__ import print_function
from __future__ import division

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

def test_select():
    x = hl.Var()
    f = hl.Func()
    f[x] = hl.select(x == 0, 31,
                     x == 2, (x * 24),
                     x == 2, 999,  # should be ignored: first condition wins
                             x)
    b = f.realize(4)
    assert b[0] == 31
    assert b[1] == 1
    assert b[2] == 48
    assert b[3] == 3

def i32(e):
    return hl.cast(hl.Int(32), e)

def f32(e):
    return hl.cast(hl.Float(32), e)

def test_minmax():
    x = hl.Var()
    f = hl.Func()
    f[x] = hl.select(x == 0,              hl.min(x, 1),
                     (x == 2) | (x == 4), i32(hl.min(f32(x), 3.2, x*2.1)),
                     x == 3,              hl.max(x, x * 3, 1, x * 4),
                                          x)
    b = f.realize(5)
    assert b[0] == 0
    assert b[1] == 1,b[1]
    assert b[2] == 2
    assert b[3] == 12
    assert b[4] == 3

if __name__ == "__main__":
    test_print_expr()
    test_print_when()
    test_select()
    test_minmax()
