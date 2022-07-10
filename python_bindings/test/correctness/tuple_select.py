import halide as hl
import numpy as np


def test_tuple_select():
    x = hl.Var('x')
    y = hl.Var('y')

    # ternary tuple_select with Expr condition
    f = hl.Func('f')
    f[x, y] = hl.tuple_select(x + y < 30, (x, y), (x-1, y-2))

    a, b = f.realize([200, 200])
    for xx in range(a.height()):
        for yy in range(a.width()):
            correct_a = xx if xx + yy < 30 else xx-1
            correct_b = yy if xx + yy < 30 else yy-2
            assert a[xx, yy] == correct_a
            assert b[xx, yy] == correct_b

    # ternary tuple_select with Tuple condition
    f = hl.Func('f')
    f[x, y] = hl.tuple_select((x < 30, y < 30), (x, y), (x-1, y-2))

    a, b = f.realize([200, 200])
    for xx in range(a.height()):
        for yy in range(a.width()):
            correct_a = xx if xx < 30 else xx-1
            correct_b = yy if yy < 30 else yy-2
            assert a[xx, yy] == correct_a
            assert b[xx, yy] == correct_b

    # multiway tuple_select with Expr condition
    f = hl.Func('f')
    f[x, y] = hl.tuple_select(x + y < 30,  (x, y),
                              x + y < 100, (x-1, y-2),
                                           (x-100, y-200))

    a, b = f.realize([200, 200])
    for xx in range(a.height()):
        for yy in range(a.width()):
            correct_a = xx if xx + yy < 30 else xx-1 if xx + yy < 100 else xx - 100
            correct_b = yy if xx + yy < 30 else yy-2 if xx + yy < 100 else yy - 200
            assert a[xx, yy] == correct_a
            assert b[xx, yy] == correct_b

    # multiway tuple_select with Tuple condition
    f = hl.Func('f')
    f[x, y] = hl.tuple_select((x < 30, y < 30),   (x, y),
                              (x < 100, y < 100), (x-1, y-2),
                                                  (x-100, y-200))

    a, b = f.realize([200, 200])
    for xx in range(a.height()):
        for yy in range(a.width()):
            correct_a = xx if xx < 30 else xx-1 if xx < 100 else xx - 100
            correct_b = yy if yy < 30 else yy-2 if yy < 100 else yy - 200
            assert a[xx, yy] == correct_a
            assert b[xx, yy] == correct_b

    # Failure case: mixing Expr and Tuple in multiway
    try:
        f = hl.Func('f')
        f[x, y] = hl.tuple_select((x < 30, y < 30), (x, y),
                                   x + y < 100,     (x-1, y-2),
                                                    (x-100, y-200))
    except hl.HalideError as e:
        assert 'tuple_select() may not mix Expr and Tuple for the condition elements.' in str(e)
    else:
        assert False, 'Did not see expected exception!'

    # Failure case: Tuples of mixed sizes
    try:
        f = hl.Func('f')
        f[x, y] = hl.tuple_select((x < 30, y < 30), (x, y, 0), (1, 2, 3, 4))
    except hl.HalideError as e:
        assert 'tuple_select() requires all Tuples to have identical sizes' in str(e)
    else:
        assert False, 'Did not see expected exception!'


if __name__ == "__main__":
    test_tuple_select()
