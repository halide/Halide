from bitcpp import bitcpp
from bitpy import bitpy
import numpy as np


def test(fn):
    # Note that in Halide, Buffer<bool> and Buffer<uint8> have identical memory
    # layout -- a bool takes an entire byte in a buffer -- but distinct types,
    # so we must construct a Buffer here that has that correct type. (If we
    # passed in a uint8 buffer, we'd fail with 'wrong type' exception.)
    #
    # Python's `array` module doesn't support boolean entries, but libraries
    # that support Buffer Protocol (eg numpy.ndarray) do, so we'll use one of those.
    input_bools = np.ndarray([4], dtype=bool)
    output_bools = np.ndarray([4], dtype=bool)

    for i in range(0, 4):
        input_bools[i] = (i & 1) != 0

    fn(input_bools, True, output_bools)
    for i in range(0, 4):
        assert output_bools[i] == True

    fn(input_bools, False, output_bools)
    for i in range(0, 4):
        assert output_bools[i] == ((i & 1) != 0)


if __name__ == "__main__":
    test(bitcpp)
    test(bitpy)
