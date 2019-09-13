import math

import halide as hl
import numpy


def test():
    def test_pattern(c):
        x = hl.Var("x")
        f = hl.Func("f")
        f[x] = x * c * (hl.f64(0.1) + hl.f64(0.2))
        for i, hl_value in enumerate(numpy.asarray(f.realize(10))):
            py_value = i * c * (0.1 + 0.2)
            check = math.isclose(hl_value, py_value)
            assert check, "{}[{}]: {} != {}".format(i, c, hl_value, py_value)

    test_pattern(0.123456789012345678)
    test_pattern(0.987654321098765432)


if __name__ == "__main__":
    test()
