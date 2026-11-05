from halide import Func, Var


def test_precompute():
    x = Var("x")

    lut = Func("lut")
    lut[x] = x * x + 1
    assert not lut.is_precomputed()
    lut.precompute()
    assert lut.is_precomputed()

    output = Func("output")
    output[x] = lut[x % 4]

    result = output.realize([8])
    assert list(result) == [1, 2, 5, 10, 1, 2, 5, 10]


def main():
    test_precompute()


if __name__ == "__main__":
    main()
