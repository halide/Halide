#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// out(x) = in(x) * x;
extern "C" HALIDE_EXPORT_SYMBOL int extern_stage(halide_buffer_t *in, halide_buffer_t *out) {
    assert(in->type == halide_type_of<int>());
    assert(out->type == halide_type_of<int>());
    if (in->host == nullptr || out->host == nullptr) {
        // We require input size = output size, and just for fun,
        // we'll require that the output size must be a multiple of 17
        if (out->is_bounds_query()) {
            out->dim[0].extent = ((out->dim[0].extent + 16) / 17) * 17;
        }
        if (in->is_bounds_query()) {
            in->dim[0].extent = out->dim[0].extent;
            in->dim[0].min = out->dim[0].min;
        }
    } else {
        assert(out->dim[0].extent % 17 == 0);
        int32_t *in_origin = (int32_t *)in->host - in->dim[0].min;
        int32_t *out_origin = (int32_t *)out->host - out->dim[0].min;
        for (int i = out->dim[0].min; i < out->dim[0].min + out->dim[0].extent; i++) {
            out_origin[i] = in_origin[i] * i;
        }
    }
    return 0;
}

void check_result(Buffer<int32_t> result) {
    for (int x = 0; x < 100; x++) {
        int32_t correct = x * x * x * 2;
        EXPECT_EQ(result(x), correct) << "x = " << x;
    }
}
}  // namespace

TEST(ExternOutputExpansion, Basic) {
    Func f, g, h;
    Var x;
    f(x) = x * x;

    g.define_extern("extern_stage", {f}, Int(32), 1);

    h(x) = g(x) * 2;

    // Compute h in 10-wide sections
    Var xo;
    h.split(x, xo, x, 10);
    f.compute_root();

    // Test first with g computed at tiles
    {
        SCOPED_TRACE("g.compute_at(h, xo)");
        g.compute_at(h, xo);
        check_result(h.realize({100}));
    }

    // Test again with g computed at the root
    {
        SCOPED_TRACE("g.compute_root()");
        g.compute_root();
        check_result(h.realize({100}));
    }
}
