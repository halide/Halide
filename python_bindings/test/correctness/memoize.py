from halide import Func, Var


def test_memoize():
    x = Var("x")

    f = Func("f")
    f[x] = 0.0
    f[x] = f[x] + 1
    f.memoize()

    output = Func("output")
    output[x] = f[x]

    result = output.realize([3])
    assert list(result) == [1., 1., 1.]


def main():
    test_memoize()


if __name__ == "__main__":
    main()
