import halide as hl
import numpy as np

from simplepy_generator import SimplePy
import simplecpp_pystub  # Needed for create_callable_from_generator("simplecpp") to work


def test_callable():
    p_int16 = hl.Param(hl.Int(16), 42)
    p_float = hl.Param(hl.Float(32), 1.0)
    p_img = hl.ImageParam(hl.UInt(8), 2)

    x = hl.Var("x")
    y = hl.Var("y")
    f = hl.Func("f")

    f[x, y] = p_img[x, y] + hl.u8(p_int16 / p_float)

    in1 = hl.Buffer(hl.UInt(8), [10, 10])
    in2 = hl.Buffer(hl.UInt(8), [10, 10])

    for i in range(10):
        for j in range(10):
            in1[i, j] = i + j * 10
            in2[i, j] = i * 10 + j

    c = f.compile_to_callable([p_img, p_int16, p_float])

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

    # Test bounds inference. Note that in Python there
    # isn't a "natural" way to create a buffer with a null host ptr
    # so we use this specific API for the purpose.
    in_bounds = hl.Buffer.make_bounds_query(hl.UInt(8), [1, 1])
    out_bounds = hl.Buffer.make_bounds_query(hl.UInt(8), [20, 20])
    c(in_bounds, 42, 1.0, out_bounds)

    assert in_bounds.defined()
    assert in_bounds.dim(0).extent() == 20
    assert in_bounds.dim(1).extent() == 20
    assert in1.dim(0).extent() == 10
    assert in1.dim(1).extent() == 10


def test_simple(callable_factory):
    x, y = hl.Var(), hl.Var()
    target = hl.get_jit_target_from_environment()

    b_in = hl.Buffer(hl.UInt(8), [2, 2])
    b_in.fill(123)

    float_in = 3.5

    b_out = hl.Buffer(hl.Float(32), [2, 2])

    def _check(offset=0):
        assert b_out[0, 0] == float_in + offset + 123
        assert b_out[0, 1] == float_in + offset + 123
        assert b_out[1, 0] == float_in + offset + 123
        assert b_out[1, 1] == float_in + offset + 123

    generator_params = {}
    simple = callable_factory(target, generator_params)

    # ----------- Positional arguments
    simple(b_in, float_in, b_out)
    _check()

    # ----------- Keyword arguments
    # Natural order
    simple(buffer_input=b_in, float_arg=float_in, simple_output=b_out)
    _check()

    # Weird order
    simple(float_arg=float_in, simple_output=b_out, buffer_input=b_in)
    _check()

    # ----------- Positional + Keywords

    # Natural order
    simple(b_in, simple_output=b_out, float_arg=float_in)
    _check()

    # Weird order
    simple(b_in, float_in, simple_output=b_out)
    _check()

    # ----------- Above set again, w/ additional GeneratorParam mixed in
    k = 42

    generator_params = {"offset": str(k)}
    simple_42 = callable_factory(target, generator_params)
    simple_42(b_in, float_in, b_out)
    _check(k)

    # ----------- Test various failure modes
    try:
        # too many positional args
        simple(b_in, float_in, 4, b_out)
    except hl.HalideError as e:
        assert "Expected at most 3 positional arguments, but saw 4." in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # too few positional args
        simple(b_in)
    except hl.HalideError as e:
        assert "Expected exactly 3 positional arguments, but saw 1." in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Inputs that can't be converted to what the receiver needs (positional)
        simple(hl.f32(3.141592), "happy", b_out)
    except hl.HalideError as e:
        assert "is not an instance of" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Inputs that can't be converted to what the receiver needs (named)
        simple(b_in, float_in, simple_output="bogus")
    except hl.HalideError as e:
        assert "is not an instance of" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Bad keyword argument
        simple(buffer_input=b_in, float_arg=float_in, funk_input=b_out)
    except hl.HalideError as e:
        assert "Unknown argument 'funk_input' specified via keyword." in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # too few keyword args
        simple(float_arg=float_in, simple_output=b_out)
    except hl.HalideError as e:
        assert (
            "Argument buffer_input was not specified by either positional or keyword argument."
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"

    try:
        # Arg specified by pos + kw
        simple(b_in, buffer_input=b_in, float_arg=float_in, simple_output=b_out)
    except hl.HalideError as e:
        assert "Argument buffer_input specified multiple times." in str(e)
    else:
        assert False, "Did not see expected exception!"


if __name__ == "__main__":
    # test_callable()

    def via_simplecpp_pystub(target, generator_params):
        return hl.create_callable_from_generator(target, "simplecpp", generator_params)

    def via_simplepy(target, generator_params):
        with hl.GeneratorContext(target):
            g = SimplePy(generator_params=generator_params)
            return g.compile_to_callable()

    test_simple(via_simplecpp_pystub)
    test_simple(via_simplepy)
