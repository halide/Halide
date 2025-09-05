#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
extern "C" HALIDE_EXPORT_SYMBOL int many_small_extern_stages_copy(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        // Give it the same shape as the output
        in->dim[0] = out->dim[0];
        in->dim[1] = out->dim[1];
    } else {
        // Check the sizes and strides match. This is not guaranteed
        // by the interface, but it should happen with this schedule
        // because we compute the input to the extern stage at the
        // same granularity as the extern stage.

        EXPECT_EQ(in->dim[0], out->dim[0]);
        EXPECT_EQ(in->dim[1], out->dim[1]);

        size_t sz = out->type.bytes() * out->dim[0].extent * out->dim[1].extent;

        // Make sure we can safely do a dense memcpy. Should be true because of the extent.
        ASSERT_EQ(out->dim[0].stride, 1) << "Bad stride", halide_error_code_bad_dimensions;
        ASSERT_EQ(out->dim[1].stride, out->dim[0].extent) << "Bad stride", halide_error_code_bad_dimensions;

        memcpy(out->host, in->host, sz);
    }
    return 0;
}
}  // namespace

TEST(ManySmallExternStagesTest, Basic) {
    Func f, g, h;
    Var x, y;

    f(x, y) = x * x + y;

    // Name of the function and the args, then types of the outputs, then dimensionality
    g.define_extern("many_small_extern_stages_copy", {f}, Int(32), 2);

    RDom r(0, 100);
    h(x, y) += r * (g(x, y) - f(x, y));

    f.compute_at(h, y);
    g.compute_at(h, y).store_root();

    Buffer<int> result = h.realize({10, 10});

    for (int yy = 0; yy < result.height(); yy++) {
        for (int xx = 0; xx < result.width(); xx++) {
            EXPECT_EQ(result(xx, yy), 0) << "result(" << xx << ", " << yy << ")";
        }
    }
}
