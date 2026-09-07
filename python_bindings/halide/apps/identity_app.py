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

    # Generated extensions use the same named-capsule protocol as
    # halide.runtime. Exceptions from that protocol must propagate rather than
    # being discarded before a buffer-protocol fallback.
    class CapsuleError(Exception):
        pass

    class BrokenCapsuleArray(np.ndarray):
        def _get_halide_buffer_t_capsule(self):
            raise CapsuleError("capsule failure")

    try:
        identity(array.view(BrokenCapsuleArray))
    except CapsuleError as e:
        assert str(e) == "capsule failure"
    else:
        assert False, "capsule protocol exception was suppressed"

    print("Success!")


if __name__ == "__main__":
    main()
