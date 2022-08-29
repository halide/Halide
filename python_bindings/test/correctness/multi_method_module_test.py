import numpy as np

import multi_method_module

def test_simple():
    buffer_input = np.ndarray([2, 2], dtype=np.uint8)
    buffer_input[0, 0] = 123
    buffer_input[0, 1] = 123
    buffer_input[1, 0] = 123
    buffer_input[1, 1] = 123

    func_input = np.ndarray([2, 2], dtype=np.uint8)
    func_input[0, 0] = 0
    func_input[0, 1] = 1
    func_input[1, 0] = 1
    func_input[1, 1] = 2

    float_arg = 3.5

    simple_output = np.ndarray([2, 2], dtype=np.float32)

    multi_method_module.simple(buffer_input, func_input, float_arg, simple_output)

    assert simple_output[0, 0] == 3.5 + 123
    assert simple_output[0, 1] == 4.5 + 123
    assert simple_output[1, 0] == 4.5 + 123
    assert simple_output[1, 1] == 5.5 + 123

def test_user_context():
    output = bytearray("\0\0\0\0", "ascii")
    multi_method_module.user_context(None, ord('q'), output)
    assert output == bytearray("qqqq", "ascii")


if __name__ == "__main__":
    test_simple()
    test_user_context()