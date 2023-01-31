"""
Simple blur.
"""

from blur import blur
import halide.imageio
import numpy as np
import sys
import timeit


def main():
    if len(sys.argv) < 3:
        print("Usage: %s input.png output.png" % sys.argv[0])
        print("e.g. %s input.png output.png 10" % sys.argv[0])
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    timing_iterations = 10

    print("Reading from %s ..." % input_path)
    input_buf_u8 = halide.imageio.imread(input_path)
    assert input_buf_u8.dtype == np.uint8
    # Convert to uint16... but remember that the blur() generator
    # is documented as only working on <= 14 bits of image; if
    # we use the upper two bits we'll get incorrect results.
    # We'll just leave it with 8 bits of useful data.
    input_buf = input_buf_u8.astype(np.uint16)
    output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

    tests = {
        "Manual": blur,
    }

    for name, fn in tests.items():
        print("Running %s... " % name, end="")
        t = timeit.Timer(lambda: fn(input_buf, output_buf))
        avg_time_sec = t.timeit(number=timing_iterations) / timing_iterations
        print("time: %fms" % (avg_time_sec * 1e3))

    output_buf_u8 = output_buf.astype(np.uint8)
    print("Saving to %s ..." % output_path)
    halide.imageio.imwrite(output_path, output_buf_u8)

    print("Success!")
    sys.exit(0)


if __name__ == "__main__":
    main()
