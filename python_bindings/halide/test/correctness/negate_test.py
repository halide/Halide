import halide as hl


def test_free_logical_not_function():
    x = hl.Var("x")

    f = hl.Func("f")
    f[x] = x > 5

    not_f = hl.Func("not_f")
    not_f[x] = hl.logical_not(f[x])

    f_out = f.realize([10])
    not_f_out = not_f.realize([10])

    for i in range(10):
        assert f_out[i] == (i > 5)
        assert not_f_out[i] == (i <= 5)


def test_member_logical_not_function():
    x = hl.Var("x")

    f = hl.Func("f")
    f[x] = x > 5

    not_f = hl.Func("not_f")
    not_f[x] = f[x].logical_not()

    f_out = f.realize([10])
    not_f_out = not_f.realize([10])

    for i in range(10):
        assert f_out[i] == (i > 5)
        assert not_f_out[i] == (i <= 5)


if __name__ == "__main__":
    test_free_logical_not_function()
    test_member_logical_not_function()
