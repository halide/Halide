#include <stdio.h>
#include <stdlib.h>

#include "HalideBuffer.h"
#include "extern_output.h"

using namespace Halide::Runtime;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int extern_stage(halide_buffer_t *input, int addend, halide_buffer_t *output) {
    // Note the final output buffer argument is unused.
    if (input->is_bounds_query()) {
        for (int d = 0; d < 2; d++) {
            // Request some range of the input buffer
            input->dim[d].min = output->dim[d].min;
            input->dim[d].extent = output->dim[d].extent;
        }
    } else if (!output->is_bounds_query()) {
        int min_x = output->dim[0].min;
        int max_x = min_x + output->dim[0].extent - 1;
        int min_y = output->dim[1].min;
        int max_y = min_y + output->dim[1].extent - 1;
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                int coords[2] = {x, y};
                *(int *)output->address_of(coords) = *(int *)input->address_of(coords) + addend;
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    const int width = 100;
    const int height = 200;
    Buffer<int, 2> input(width, height);
    input.fill([](int x, int y) { return rand() % 256; });
    int addend = 20;
    Buffer<int, 2> output(width, height);

    extern_output(input, addend, output);

    output.for_each_element([&](int x, int y) {
        int correct = input(x, y) * 2 + addend;
        int actual = output(x, y);
        if (actual != correct) {
            printf("output(%d, %d) = %d instead of %d %d\n",
                   x, y, actual, correct, input(x, y));
            exit(-1);
        }
    });

    printf("Success!\n");
    return 0;
}
