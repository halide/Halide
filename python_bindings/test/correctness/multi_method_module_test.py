import numpy as np

from multi_method_module import simplecpp, user_context


def test_simplecpp():
    buffer_input = np.ndarray([2, 2], dtype=np.uint8)
    buffer_input[0, 0] = 123
    buffer_input[0, 1] = 123
    buffer_input[1, 0] = 123
    buffer_input[1, 1] = 123

    float_arg = 3.5

    simple_output = np.ndarray([2, 2], dtype=np.float32)

    simplecpp(buffer_input, float_arg, simple_output)

    assert simple_output[0, 0] == 3.5 + 123
    assert simple_output[0, 1] == 3.5 + 123
    assert simple_output[1, 0] == 3.5 + 123
    assert simple_output[1, 1] == 3.5 + 123


def test_user_context():
    output = bytearray("\0\0\0\0", "ascii")
    user_context(None, ord("q"), output)
    assert output == bytearray("qqqq", "ascii")


def test_aot_call_failure_throws_exception():
    buffer_input = np.zeros([2, 2], dtype=np.float32)  # wrong type
    float_arg = 3.5
    simple_output = np.zeros([2, 2], dtype=np.float32)

    try:
        simplecpp(buffer_input, float_arg, simple_output)
    except RuntimeError as e:
        assert "Halide Runtime Error: -3" in str(e), str(e)
    else:
        assert False, "Did not see expected exception, saw: " + str(e)


if __name__ == "__main__":
    test_simplecpp()
    test_user_context()
    test_aot_call_failure_throws_exception()
