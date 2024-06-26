#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

extern "C" HALIDE_EXPORT_SYMBOL int copy_plus_xcoord(halide_buffer_t *input, int tile_extent_x, int tile_extent_y, halide_buffer_t *output) {
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
        assert(output->dim[0].extent <= tile_extent_x);
        assert(output->dim[1].extent <= tile_extent_y);
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
    Func input;
    Var x, y;
    input(x, y) = x * y;

    // We pass the tile size of the extern stage to the extern stage
    // only to test that it is in fact being tiled.
    const int extern_tile_size = 10;

    Func output;
    output.define_extern("copy_plus_xcoord", {input, extern_tile_size, extern_tile_size}, Int(32), {x, y});

    Var xo, yo;
    output.compute_root()
        .tile(x, y, xo, yo, x, y, extern_tile_size, extern_tile_size)
        .parallel(yo);

    input.compute_at(output, xo);

    Buffer<int32_t> buf = output.realize({75, 35});  // Use uneven splits.

    for (int y = 0; y < buf.height(); y++) {
        for (int x = 0; x < buf.width(); x++) {
            assert(buf(x, y) == x * y + x);
        }
    }

    printf("Success!\n");
    return 0;
}
