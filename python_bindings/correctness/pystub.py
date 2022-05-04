
import halide as hl

import simple_stub
import complex_stub

from simplepy_generator import SimplePy
from complexpy_generator import ComplexPy

def _realize_and_check(f, offset = 0):
    b = hl.Buffer(hl.Float(32), [2, 2])
    f.realize(b)

    assert b[0, 0] == 3.5 + offset + 123
    assert b[0, 1] == 4.5 + offset + 123
    assert b[1, 0] == 4.5 + offset + 123
    assert b[1, 1] == 5.5 + offset + 123


def test_simple(cls):
    x, y = hl.Var(), hl.Var()
    target = hl.get_jit_target_from_environment()
    ctx = hl.GeneratorContext(target)

    b_in = hl.Buffer(hl.UInt(8), [2, 2])
    b_in.fill(123)
    for xx in range(2):
        for yy in range(2):
            b_in[xx, yy] += xx + yy

    # ----------- Inputs by-position
    f = cls.call(ctx, b_in, 3.5)
    _realize_and_check(f)

    # ----------- Inputs by-name
    f = cls.call(ctx, buffer_input=b_in, float_arg=3.5)
    _realize_and_check(f)

    f = cls.call(ctx, float_arg=3.5, buffer_input=b_in)
    _realize_and_check(f)

    # ----------- Above set again, w/ GeneratorParam mixed in
    k = 42

    gp = { "offset": k }

    # (positional)
    f = cls.call(ctx, b_in, 3.5, offset=k)
    _realize_and_check(f, k)

    # (keyword)
    f = cls.call(ctx, offset=k, buffer_input=b_in, float_arg=3.5)
    _realize_and_check(f, k)

    f = cls.call(ctx, buffer_input=b_in, offset=k, float_arg=3.5)
    _realize_and_check(f, k)

    f = cls.call(ctx, buffer_input=b_in, offset=k, float_arg=3.5)
    _realize_and_check(f, k)

    f = cls.call(ctx, buffer_input=b_in, float_arg=3.5, offset=k)
    _realize_and_check(f, k)

    # ----------- Test various failure modes
    try:
        # Inputs w/ mixed by-position and by-name
        f = cls.call(ctx, b_in, float_arg=3.5)
    except RuntimeError as e:
        assert 'Cannot use both positional and keyword arguments for inputs.' in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # too many positional args
        f = cls.call(ctx, b_in, 3.5, 4)
    except RuntimeError as e:
        assert 'Expected exactly 2 positional args for inputs, but saw 3.' in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # too few positional args
        f = cls.call(ctx, b_in)
    except RuntimeError as e:
        assert 'Expected exactly 2 positional args for inputs, but saw 1.' in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # Inputs that can't be converted to what the receiver needs (positional)
        f = cls.call(ctx, hl.f32(3.141592), "happy")
    except RuntimeError as e:
        assert 'Unable to cast Python instance' in str(e) or \
               'requires an ImageParam or Buffer' in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # Inputs that can't be converted to what the receiver needs (named)
        f = cls.call(ctx, b_in, float_arg="bogus")
    except RuntimeError as e:
        assert 'Unable to cast Python instance' in str(e) or \
               'requires a Param (or scalar literal) argument' in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # Input specified by both pos and kwarg
        f = cls.call(ctx, b_in, 3.5, float_arg=4.5)
    except RuntimeError as e:
        assert "Cannot use both positional and keyword arguments for inputs." in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # Bad input name
        f = cls.call(ctx, buzzer_input=b_in, float_arg=3.5, offset=k)
    except RuntimeError as e:
        assert "has no GeneratorParam named: buzzer_input" in str(e) or \
               "has no GeneratorParam(s) named: ['buzzer_input']" in str(e)
    else:
        assert False, 'Did not see expected exception!'

    try:
        # Bad gp name
        f = cls.call(ctx, buffer_input=b_in, float_arg=3.5, offset=k, nonexistent_generator_param="wat")
    except RuntimeError as e:
        assert "has no GeneratorParam named: nonexistent_generator_param" in str(e) or \
               "has no GeneratorParam(s) named: ['nonexistent_generator_param']" in str(e)
    else:
        assert False, 'Did not see expected exception!'

def _make_constant_image(type):
    constant_image = hl.Buffer(type, [32, 32, 3], 'constant_image')
    for x in range(32):
        for y in range(32):
            for c in range(3):
                constant_image[x, y, c] = x + y + c
    return constant_image

def test_complex(cls):
    constant_image = _make_constant_image(hl.UInt(8))
    constant_image_u16 = _make_constant_image(hl.UInt(16))
    input = hl.ImageParam(hl.UInt(8), 3, 'input')
    input.set(constant_image)

    x, y, c = hl.Var(), hl.Var(), hl.Var()
    target = hl.get_jit_target_from_environment()
    ctx = hl.GeneratorContext(target)

    float_arg = 1.25
    int_arg = 33

    r = cls.call(ctx,
            typed_buffer_input=constant_image,
            untyped_buffer_input=constant_image,
            simple_input=constant_image,
            float_arg=float_arg,
        #     int_arg=int_arg,
        #     extra_input=constant_image_u16,
        #     vectorize=True,
        #     # We can put GeneratorParams anywhere in the list we want --
        #     # they will be examined and applied before anything else --
        #     # but it's usually better form to put them all at the end.
        #     #
        #     # We can specify a halide Type via string or object here
        #     simple_input__type=hl.UInt(8),
        #     untyped_buffer_input__type="uint8",
        #     untyped_buffer_output__type="uint8",
        #     untyped_buffer_output__dim=3,
        #     # Can specify a list-of-types for Tuple output
        #     tuple_output__type=[hl.Float(32), hl.Float(32)],
        #     # Alternately, we could specify comma-delimited string:
        #     # tuple_output__type="float32,float32",
        # )
            int_arg=[ int_arg, int_arg ],
            extra_func_input=func_input,
            generator_params = {
                "untyped_buffer_output.type": hl.UInt(8),
                "vectorize": True
            })

    # return value is a tuple; unpack separately to avoid
    # making the callsite above unreadable
    (simple_output,
        tuple_output,
        typed_buffer_output,
        untyped_buffer_output,
        static_compiled_buffer_output,
        scalar_output,
        extra_func_output) = r

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
                assert expected1 == actual1, "Expected1 %s Actual1 %s" % (expected1, actual1)
                assert expected2 == actual2, "Expected2 %s Actual1 %s" % (expected2, actual2)

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
            expected = x + y + 1
            actual = b[x, y]
            assert expected == actual, "Expected %s Actual %s" % (expected, actual)

if __name__ == "__main__":
    test_simple(simple_stub)
    test_complex(complex_stub)
    test_simple(SimplePy)
    test_complex(ComplexPy)
