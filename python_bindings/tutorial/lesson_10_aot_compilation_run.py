# Before reading this file, see lesson_10_aot_compilation_generate.py

# This is the code that actually uses the Halide pipeline we've
# compiled. It does not depend on libHalide, so we won't do
# "import halide".
#
# Instead, it depends on the header file that lesson_10_generate
# produced when we ran it:
import lesson_10_halide

import numpy as np

def main():
    # Have a look at the generated files above (they won't exist until you've run
    # lesson_10_generate): lesson_10_halide.py.c, lesson_10_halide.h
    #
    # In the header file, the generated function is represented like this:
    # int lesson_10_halide(halide_buffer_t*, uint8_t, halide_buffer_t*);
    #
    # lesson_10_halide.py.c creates a Python wrapper around this function.
    # Buffers are converted using the Python buffer API:
    #
    # https://docs.python.org/2/c-api/buffer.html
    # https://docs.python.org/3/c-api/buffer.html
    #
    # In other words, you can pass numpy arrays directly to the generated
    # code.

    # Let's make some input data to test with:
    input = np.empty((640, 480), dtype=np.uint8, order='F')
    for y in range(480):
        for x in range(640):
            input[x, y] = x ^ (y + 1)

    # And the memory where we want to write our output:
    output = np.empty((640,480), dtype=np.uint8, order='F')

    offset_value = 5

    lesson_10_halide.lesson_10_halide(input, offset_value, output)

    # Now let's check the filter performed as advertised. It was
    # supposed to add the offset to every input pixel.
    correct_val = np.empty((1), dtype=np.uint8)
    for y in range(480):
        for x in range(640):
            input_val = input[x, y]
            output_val = output[x, y]
            correct_val[0] = input_val
            # we add over a uint8 value (will properly model overflow)
            correct_val[0] += offset_value
            if output_val != correct_val[0]:
                raise Exception("output(%d, %d) was %d instead of %d" % (
                       x, y, output_val, correct_val))
                #return -1

    # Everything worked!
    print("Success!")
    return 0


if __name__ == "__main__":
    main()
