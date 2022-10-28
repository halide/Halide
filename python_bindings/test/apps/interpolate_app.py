"""
Shell for running Fast image interpolation using a pyramid.
"""

from interpolate import interpolate
from interpolate_Mullapudi2016 import interpolate_Mullapudi2016
import imageio
import numpy as np
import sys
from timeit import Timer


def main():
    if len(sys.argv) < 3:
        print("Usage: %s input.png output.png" % sys.argv[0])
        print("e.g. %s input.png output.png 10" % sys.argv[0])
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    timing_iterations = 10

    print("Reading from %s ..." % input_path)
    input_buf_u8 = imageio.imread(input_path)
    assert input_buf_u8.dtype == np.uint8
    # Convert to float32 in range [0..1]
    # TODO: clean this up after #7125 lands
    input_buf = np.copy(input_buf_u8, order="F").astype(np.float32) / 255.0
    # TODO: clean this up after #7125 lands
    h = input_buf.shape[0]
    w = input_buf.shape[1]
    output_buf = np.empty([h, w, 3], dtype=input_buf.dtype, order="F")

    tests = {
        "Manual": interpolate,
        "Mullapudi2016": interpolate_Mullapudi2016,
    }

    for name, fn in tests.items():
        print("Running %s... " % name, end="")
        t = Timer(lambda: fn(input_buf, output_buf))
        avg_time_sec = t.timeit(number=timing_iterations) / timing_iterations
        print("time: %fms" % (avg_time_sec * 1e3))

    output_buf *= 255.0
    output_buf_u8 = output_buf.astype(np.uint8)

    print("Saving to %s ..." % output_path)
    imageio.imsave(output_path, output_buf_u8)

    print("Success!")
    sys.exit(0)


if __name__ == "__main__":
    main()
