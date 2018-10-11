#include "Halide.h"
#include <stdio.h>

#include "test/common/halide_test_dirs.h"

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT
int copy_plus_xcoord(halide_buffer_t *input, halide_buffer_t *output) {
    // Note the final output buffer argument is unused.
    if (input->is_bounds_query()) {
        for (int d = 0; d < 2; d++) {
            // Request some range of the input buffer
            input->dim[d].min = output->dim[d].min;
            input->dim[d].extent = output->dim[d].extent;
        }
    } else {
        int min_x = output->dim[0].min;
        int max_x = min_x + output->dim[0].extent - 1;
        int min_y = output->dim[1].min;
        int max_y = min_y + output->dim[1].extent - 1;
        printf("[%d %d] x [%d %d]\n", min_x, max_x, min_y, max_y);
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                int coords[2] = { x, y };
                *(int *)output->address_of(coords) = *(int *)input->address_of(coords) + x;
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    Func input;
    Var x, y;
    input(x, y) = x*y;

    Func output;
    output.compute_root().define_extern("copy_plus_xcoord", {input}, Int(32), {x, y});

    input.compute_root();

    Var xo, yo;
    output.tile(x, y, xo, yo, x, y, 10, 10);
    output.serial(xo);
    output.serial(yo);

    Func output2;
    output2(x, y) = output(x, y);
    output2.compute_root();
    
    Buffer<int32_t> buf = output2.realize(100, 100);

    for (int y = 0; y < buf.height(); y++) {
        for (int x = 0; x < buf.width(); x++) {
            assert(buf(x, y) == x*y + x);
        }
    }

    printf("Success!\n");
    return 0;
}
