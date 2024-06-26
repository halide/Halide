#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

extern "C" HALIDE_EXPORT_SYMBOL int copy_row_plus_xcoord(halide_buffer_t *input, halide_buffer_t *output) {
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
        // One of the dimensions should have extent 1.
        assert(output->dim[0].extent == 1 || output->dim[1].extent == 1);
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                int coords[2] = {x, y};
                *(int *)output->address_of(coords) = *(int *)input->address_of(coords) + x;
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    // Try making only one of each dimension of a 2D extern stage extern.
    for (int extern_dim = 0; extern_dim < 2; extern_dim++) {
        Func input;
        Var x, y;
        input(x, y) = x * y;

        Func output;
        output.define_extern("copy_row_plus_xcoord", {input}, Int(32), {x, y});

        if (extern_dim == 0) {
            output.compute_root().reorder(y, x).serial(x);  // Change loop from extern to serial.
            input.compute_at(output, x);
        } else {
            output.compute_root().serial(y);  // Change loop from extern to serial.
            input.compute_at(output, y);
        }

        Buffer<int32_t> buf = output.realize({100, 100});

        for (int y = 0; y < buf.height(); y++) {
            for (int x = 0; x < buf.width(); x++) {
                assert(buf(x, y) == x * y + x);
            }
        }
    }

    printf("Success!\n");
    return 0;
}
