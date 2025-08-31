#include "Halide.h"
#include <gtest/gtest.h>

#include <algorithm>

using namespace Halide;

namespace {
// Use an extern stage to do a sort
extern "C" HALIDE_EXPORT_SYMBOL int sort_buffer(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        in->dim[0].min = out->dim[0].min;
        in->dim[0].extent = out->dim[0].extent;
    } else {
        memcpy(out->host, in->host, out->dim[0].extent * out->type.bytes());
        float *out_start = (float *)out->host;
        float *out_end = out_start + out->dim[0].extent;
        std::sort(out_start, out_end);
        out->set_host_dirty();
    }
    return 0;
}
}  // namespace

TEST(ExternSortTest, Basic) {
    Var x;

    Func data;
    data(x) = sin(x);
    data.compute_root();

    Func sorted;
    sorted.define_extern("sort_buffer", {data}, Float(32), 1);

    Buffer<float> output = sorted.realize({100});

    Buffer<float> reference = lambda(x, sin(x)).realize({100});
    std::sort(&reference(0), &reference(100));

    for (int i = 0; i < output.width(); i++) {
        EXPECT_EQ(output(i), reference(i));
    }
}
