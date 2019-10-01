import math
import sys
import warnings

import halide as hl
import numpy


class AssertWarnsContext:
    def __init__(self, warn):
        self.expected = warn
        self.occurred = False

    def __bool__(self):
        return self.occurred

    def __enter__(self):
        self.warnings_manager = warnings.catch_warnings(record=True)
        self.warnings = self.warnings_manager.__enter__()
        return self

    def __exit__(self, exc_type, exc_value, tb):
        self.warnings_manager.__exit__(exc_type, exc_value, tb)
        if exc_type is not None:
            return
        self.occurred = False
        for m in self.warnings:
            if not isinstance(m.message, self.expected):
                continue
            self.occurred = True


def test():
    def test_pattern(c):
        x = hl.Var("x")
        f = hl.Func("f")
        f[x] = x * hl.f64(c) * (hl.f64(0.1) + hl.f64(0.2))
        for i, hl_value in enumerate(numpy.asarray(f.realize(10))):
            py_value = i * c * (0.1 + 0.2)
            check = math.isclose(hl_value, py_value)
            assert check, "{}[{}]: {} != {}".format(i, c, hl_value, py_value)

    test_pattern(0.123456789012345678)
    test_pattern(0.987654321098765432)

    x = hl.Var("x")
    with AssertWarnsContext(RuntimeWarning) as ctx:
        x + 0.123456789012345678
    assert ctx.occurred, "RuntimeWarning didn't occur."

    with AssertWarnsContext(RuntimeWarning) as ctx:
        x + 0.75  # 0.5 + 0.25
    assert not ctx.occurred, "RuntimeWarning occurred."


if __name__ == "__main__":
    test()
