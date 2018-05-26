from __future__ import print_function

import addconstant
import array
import bit
import sys


def test():
    bool_constant = True
    input_u1 = array.array('B', [0, 1, 0, 1])
    output_u1 = array.array('B', [0, 1, 0, 1])

    try:
        bit.bit(
            bool_constant, input_u1, output_u1
        )
    except NotImplementedError:
        pass  # OK - that's what we expected.
    else:
        print("Expected Exception not raised.", file=sys.stderr)
        exit(1)


if __name__ == "__main__":
  test()
