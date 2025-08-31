#include "Halide.h"
#include "halide_test_dirs.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
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

void check_output(Buffer<int32_t> buf) {
    for (int y = 0; y < buf.height(); y++) {
        for (int x = 0; x < buf.width(); x++) {
            int correct = x * y + x;
            EXPECT_EQ(buf(x, y), correct) << "x = " << x << ", y = " << y;
        }
    }
}
}  // namespace

TEST(ExternPartialTest, Basic) {
    Func input, output;
    Var x, y;

    input(x, y) = x * y;
    input.compute_at(output, y);

    output.define_extern("copy_row_plus_xcoord", {input}, Int(32), {x, y});
    output.compute_root().serial(y);

    check_output(output.realize({100, 100}));
}

TEST(ExternPartialTest, Reorder) {
    Func input, output;
    Var x, y;

    input(x, y) = x * y;
    input.compute_at(output, x);

    output.define_extern("copy_row_plus_xcoord", {input}, Int(32), {x, y});
    output.compute_root().reorder(y, x).serial(x);

    check_output(output.realize({100, 100}));
}
