import sys

import halide as hl
import numpy as np
from identity import identity


def main():
    size = 100

    ##
    # First test using Numpy buffers

    expected = np.arange(size, dtype=np.int32)
    array = np.empty_like(expected)

    identity(array)

    if not np.array_equal(array, expected):
        sys.exit("np.array failure!")

    ##
    # Second test using hl.Buffer as a wrapper

    expected = np.arange(-size, 0, dtype=np.int32)
    array = np.empty_like(expected)
    array_hl = hl.Buffer(array)
    array_hl.set_min([-size])
    identity(array_hl)

    if not np.array_equal(array, expected):
        sys.exit("hl.Buffer failure!")

    print("Success!")


if __name__ == '__main__':
    main()
