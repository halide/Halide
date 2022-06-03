import halide as hl
import numpy as np

def test_callable():
    p_int16 = hl.Param(hl.Int(16), 42)
    p_float = hl.Param(hl.Float(32), 1.0)
    p_img = hl.ImageParam(hl.UInt(8), 2)

    x = hl.Var('x')
    y = hl.Var('y')
    f = hl.Func('f')

    f[x, y] = p_img[x, y] + hl.u8(p_int16 / p_float)

    in1 = hl.Buffer(hl.UInt(8), [10, 10])
    in2 = hl.Buffer(hl.UInt(8), [10, 10])

    for i in range(10):
        for j in range(10):
            in1[i, j] = i + j * 10
            in2[i, j] = i * 10 + j

    c = f.compile_to_callable([p_img, p_int16, p_float]);

    out1 = hl.Buffer(hl.UInt(8), [10, 10])
    c(in1, 42, 1.0, out1)

    out2 = hl.Buffer(hl.UInt(8), [10, 10])
    c(in2, 22, 2.0, out2)

    out3 = hl.Buffer(hl.UInt(8), [10, 10])
    c(in1, 12, 1.0, out3)

    out4 = hl.Buffer(hl.UInt(8), [10, 10])
    c(in2, 16, 1.0, out4)

    for i in range(10):
        for j in range(10):
            assert out1[i, j] == i + j * 10 + 42
            assert out2[i, j] == i * 10 + j + 11
            assert out3[i, j] == i + j * 10 + 12
            assert out4[i, j] == i * 10 + j + 16

    # Test bounds inference
    in_bounds = hl.Buffer.make_bounds_query(hl.UInt(8), [1, 1])
    out_bounds = hl.Buffer.make_bounds_query(hl.UInt(8), [20, 20])
    c(in_bounds, 42, 1.0, out_bounds)

    assert in_bounds.defined()
    assert in_bounds.dim(0).extent() == 20
    assert in_bounds.dim(1).extent() == 20
    assert in1.dim(0).extent() == 10
    assert in1.dim(1).extent() == 10

if __name__ == "__main__":
    test_callable()
