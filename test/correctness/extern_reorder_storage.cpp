#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// An extern stage that translates.
extern "C" HALIDE_EXPORT_SYMBOL int copy_and_check_strides(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        for (int i = 0; i < 2; i++) {
            in->dim[i].min = out->dim[i].min;
            in->dim[i].extent = out->dim[i].extent;
        }
    } else if (!out->is_bounds_query()) {
        // Check that the storage has been reordered.
        assert(out->dim[0].stride > out->dim[1].stride);
        Runtime::Buffer<uint8_t> out_buf(*out);
        out_buf.copy_from(Halide::Runtime::Buffer<uint8_t>(*in));
    }
    return 0;
}
}  // namespace

TEST(ExternReorderStorageTest, Basic) {
    Var x, y;

    constexpr int W = 30, H = 20;
    Buffer<uint8_t> input_buffer(W, H);
    for (int i = 0; i < H; i++) {
        for (int j = 0; j < W; j++) {
            input_buffer(j, i) = i + j;
        }
    }

    // Define a pipeline that uses an input image in an extern stage
    // only and do bounds queries.
    ImageParam input(UInt(8), 2);
    Func f, g;

    f.define_extern("copy_and_check_strides", {input}, UInt(8), {x, y});
    g(x, y) = f(x, y);

    f.compute_root().reorder_storage(y, x);

    input.set(input_buffer);
    Buffer<uint8_t> output = g.realize({W, H});
    for (int i = 0; i < H; i++) {
        for (int j = 0; j < W; j++) {
            EXPECT_EQ(output(j, i), i + j);
        }
    }
}
