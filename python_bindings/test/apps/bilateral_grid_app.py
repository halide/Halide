"""
Bilateral histogram.
"""

from bilateral_grid import bilateral_grid
from bilateral_grid_Adams2019 import bilateral_grid_Adams2019
from bilateral_grid_Li2018 import bilateral_grid_Li2018
from bilateral_grid_Mullapudi2016 import bilateral_grid_Mullapudi2016
import halide.imageio
import numpy as np
import sys
from timeit import Timer


def main():
    if len(sys.argv) < 4:
        print("Usage: %s input.png output.png range_sigma" % sys.argv[0])
        print("e.g. %s input.png output.png 0.1 10" % sys.argv[0])
        sys.exit(1)

    input_path = sys.argv[1]
    r_sigma = float(sys.argv[2])
    output_path = sys.argv[3]
    timing_iterations = 10

    print("Reading from %s ..." % input_path)
    input_buf_u8 = halide.imageio.imread(input_path)
    assert input_buf_u8.dtype == np.uint8
    # Convert to float32
    input_buf = input_buf_u8.astype(np.float32)
    input_buf /= 255.0
    output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

    tests = {
        "Manual": bilateral_grid,
        "Adams2019": bilateral_grid_Adams2019,
        #
        # TODO: Don't test the Li2018-autoscheduled version here;
        # it schedules the histogram stage with compute_root(),
        # which (for a 4MP input image) attempts to allocate 124GB (!)...
        #
        # "Li2018": bilateral_grid_Li2018,
        #
        "Mullapudi2016": bilateral_grid_Mullapudi2016,
    }

    for name, fn in tests.items():
        print("Running %s... " % name, end="")
        t = Timer(lambda: fn(input_buf, r_sigma, output_buf))
        avg_time_sec = t.timeit(number=timing_iterations) / timing_iterations
        print("time: %fms" % (avg_time_sec * 1e3))

    output_buf *= 255.0
    output_buf_u8 = output_buf.astype(np.uint8)
    print("Saving to %s ..." % output_path)
    halide.imageio.imwrite(output_path, output_buf_u8)

    print("Success!")
    sys.exit(0)


if __name__ == "__main__":
    main()
