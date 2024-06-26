import halide as hl

import simplecpp_pystub
import complexcpp_pystub

from simplepy_generator import SimplePy
from complexpy_generator import ComplexPy


def _realize_and_check(f, offset=0):
    b = hl.Buffer(hl.Float(32), [2, 2])
    f.realize(b)

    assert b[0, 0] == 3.5 + offset + 123
    assert b[0, 1] == 4.5 + offset + 123
    assert b[1, 0] == 4.5 + offset + 123
    assert b[1, 1] == 5.5 + offset + 123


def test_simple(cls):
    x, y = hl.Var(), hl.Var()

    b_in = hl.Buffer(hl.UInt(8), [2, 2])
    b_in.fill(123)
    for xx in range(2):
        for yy in range(2):
            b_in[xx, yy] += xx + yy

    # ----------- Inputs by-position
    f = cls.call(b_in, 3.5)
    _realize_and_check(f)

    # ----------- Inputs by-name
    f = cls.call(buffer_input=b_in, float_arg=3.5)
    _realize_and_check(f)

    f = cls.call(float_arg=3.5, buffer_input=b_in)
    _realize_and_check(f)

    # ----------- Above set again, w/ GeneratorParam mixed in
    k = 42

    gp = {"offset": k}

    # (positional)
    f = cls.call(b_in, 3.5, generator_params=gp)
    _realize_and_check(f, k)

    # (keyword)
    f = cls.call(generator_params=gp, buffer_input=b_in, float_arg=3.5)
    _realize_and_check(f, k)

    f = cls.call(buffer_input=b_in, generator_params=gp, float_arg=3.5)
    _realize_and_check(f, k)

    f = cls.call(buffer_input=b_in, generator_params=gp, float_arg=3.5)
    _realize_and_check(f, k)

    f = cls.call(buffer_input=b_in, float_arg=3.5, generator_params=gp)

    # Inputs w/ mixed by-position and by-name should be ok
    f = cls.call(b_in, float_arg=3.5, generator_params=gp)
    _realize_and_check(f, k)

    # ----------- Test various failure modes
    try:
        # too many positional args
        f = cls.call(b_in, 3.5, 4)
    except hl.HalideError as e:
        assert "allows at most 2 positional args, but 3 were specified." in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # too few positional args
        f = cls.call(b_in)
    except hl.HalideError as e:
        assert "requires 2 args, but 1 were specified." in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Inputs that can't be converted to what the receiver needs (positional)
        f = cls.call(hl.f32(3.141592), "happy")
    except hl.HalideError as e:
        assert "Input buffer_input requires an ImageParam or Buffer argument" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Inputs that can't be converted to what the receiver needs (named)
        f = cls.call(b_in, float_arg="bogus")
    except hl.HalideError as e:
        assert (
            "Input float_arg requires a Param (or scalar literal) argument when using call"
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"

    try:
        # Input specified by both pos and kwarg
        f = cls.call(b_in, 3.5, float_arg=4.5)
    except hl.HalideError as e:
        assert "Input float_arg specified multiple times." in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # generator_params is not a dict
        f = cls.call(b_in, 3.5, generator_params=[1, 2, 3])
    except hl.HalideError as e:
        assert "generator_params must be a dict" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Bad gp name
        f = cls.call(b_in, 3.5, generator_params={"foo": 0})
    except hl.HalideError as e:
        assert "has no GeneratorParam" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Bad input name
        f = cls.call(buzzer_input=b_in, float_arg=3.5, generator_params=gp)
    except hl.HalideError as e:
        assert "Unknown input 'buzzer_input' specified via keyword argument" in str(e)
    else:
        assert False, "Did not see expected exception!"

    try:
        # Bad gp name
        f = cls.call(
            buffer_input=b_in,
            float_arg=3.5,
            generator_params=gp,
            nonexistent_generator_param="wat",
        )
    except hl.HalideError as e:
        assert (
            "Unknown input 'nonexistent_generator_param' specified via keyword argument"
            in str(e)
        )
    else:
        assert False, "Did not see expected exception!"


def _make_constant_image(type):
    constant_image = hl.Buffer(type, [32, 32, 3], "constant_image")
    for x in range(32):
        for y in range(32):
            for c in range(3):
                constant_image[x, y, c] = x + y + c
    return constant_image


def test_complex(cls, extra_input_name=""):
    constant_image = _make_constant_image(hl.UInt(8))
    constant_image_u16 = _make_constant_image(hl.UInt(16))
    input = hl.ImageParam(hl.UInt(8), 3, "input")
    input.set(constant_image)

    x, y, c = hl.Var(), hl.Var(), hl.Var()

    float_arg = 1.25
    int_arg = 33
    gp = {
        "simple_input.type": hl.UInt(8),
        "tuple_output.type": [hl.Float(32), hl.Float(32)],
        "untyped_buffer_input.type": hl.UInt(8),
        "untyped_buffer_output.dim": 3,
        "untyped_buffer_output.type": hl.UInt(8),
        "vectorize": True,
    }
    kwargs = {
        "typed_buffer_input": constant_image,
        "untyped_buffer_input": constant_image,
        "simple_input": constant_image,
        "float_arg": float_arg,
        "int_arg": int_arg,
        "generator_params": gp,
    }
    if len(extra_input_name):
        gp["extra_input_name"] = extra_input_name
        kwargs[extra_input_name] = constant_image_u16

    r = cls.call(**kwargs)

    # return value is a tuple; unpack separately to avoid
    # making the callsite above unreadable
    (
        simple_output,
        tuple_output,
        typed_buffer_output,
        untyped_buffer_output,
        static_compiled_buffer_output,
        scalar_output,
        extra_func_output,
    ) = r

    b = simple_output.realize([32, 32, 3], target)
    assert b.type() == hl.Float(32)
    for x in range(32):
        for y in range(32):
            for c in range(3):
                expected = constant_image[x, y, c]
                actual = b[x, y, c]
                assert expected == actual, "Expected %s Actual %s" % (expected, actual)

    b = tuple_output.realize([32, 32, 3], target)
    assert b[0].type() == hl.Float(32)
    assert b[1].type() == hl.Float(32)
    assert len(b) == 2
    for x in range(32):
        for y in range(32):
            for c in range(3):
                expected1 = constant_image[x, y, c] * float_arg
                expected2 = expected1 + int_arg
                actual1, actual2 = b[0][x, y, c], b[1][x, y, c]
                assert expected1 == actual1, "Expected1 %s Actual1 %s" % (
                    expected1,
                    actual1,
                )
                assert expected2 == actual2, "Expected2 %s Actual1 %s" % (
                    expected2,
                    actual2,
                )

    # TODO: Output<Buffer<>> has additional behaviors useful when a Stub
    # is used within another Generator; this isn't yet implemented since there
    # isn't yet Python bindings for Generator authoring. This section
    # of the test may need revision at that point.
    b = typed_buffer_output.realize([32, 32, 3], target)
    assert b.type() == hl.Float(32)
    for x in range(32):
        for y in range(32):
            for c in range(3):
                expected = constant_image[x, y, c]
                actual = b[x, y, c]
                assert expected == actual, "Expected %s Actual %s" % (expected, actual)

    b = untyped_buffer_output.realize([32, 32, 3], target)
    assert b.type() == hl.UInt(8)
    for x in range(32):
        for y in range(32):
            for c in range(3):
                expected = constant_image[x, y, c]
                actual = b[x, y, c]
                assert expected == actual, "Expected %s Actual %s" % (expected, actual)

    b = static_compiled_buffer_output.realize([4, 4, 1], target)
    assert b.type() == hl.UInt(8)
    for x in range(4):
        for y in range(4):
            for c in range(1):
                expected = constant_image[x, y, c] + 42
                actual = b[x, y, c]
                assert expected == actual, "Expected %s Actual %s" % (expected, actual)

    b = scalar_output.realize([], target)
    assert b.type() == hl.Float(32)
    assert b[()] == 34.25

    b = extra_func_output.realize([32, 32], target)
    assert b.type() == hl.Float(64)
    for x in range(32):
        for y in range(32):
            if len(extra_input_name):
                expected = x + y + 1
            else:
                expected = 0
            actual = b[x, y]
            assert expected == actual, "Expected %s Actual %s" % (expected, actual)


if __name__ == "__main__":
    target = hl.get_jit_target_from_environment()
    with hl.GeneratorContext(target):
        test_simple(simplecpp_pystub)
        test_complex(complexcpp_pystub)
        test_complex(complexcpp_pystub, extra_input_name="foo_input")
        test_simple(SimplePy)
        test_complex(ComplexPy)
        test_complex(ComplexPy, extra_input_name="foo_input")
