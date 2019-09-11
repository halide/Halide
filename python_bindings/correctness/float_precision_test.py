import halide as hl
import numpy


def test():
    def test_pattern(c):
        x = hl.Var("x")
        f = hl.Func("f")
        f[x] = x * c
        for i, v in enumerate(numpy.asarray(f.realize(10))):
            assert v == (i * c), "{}[{}]: {} != {}".format(i, c, v, (i * c))

    test_pattern(0.123456789012345678)
    test_pattern(0.987654321098765432)


if __name__ == "__main__":
    test()
