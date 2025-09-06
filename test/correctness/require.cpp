#include "Halide.h"
#include <gtest/gtest.h>
#include <memory>

using namespace Halide;

namespace {

// TODO: convert to error test

int error_occurred = false;
void halide_error(JITUserContext *ctx, const char *msg) {
    error_occurred = true;
}

}  // namespace

class RequireTest : public ::testing::TestWithParam<int> {
};

TEST_P(RequireTest, VectorWidth) {
    Target target = get_jit_target_from_environment();
    int vector_width = GetParam();

    const int32_t kPrime1 = 7829;
    const int32_t kPrime2 = 7919;

    int32_t realize_width = vector_width ? vector_width : 1;

    Buffer<int32_t> result;
    Param<int32_t> p1, p2;
    Var x;
    Func s, f;
    s(x) = p1 + p2;
    f(x) = require(s(x) == kPrime1,
                   s(x) * kPrime2 + x,
                   "The parameters should add to exactly", kPrime1, "but were", s(x), "for vector_width", vector_width);
    if (vector_width) {
        s.vectorize(x, vector_width).compute_root();
        f.vectorize(x, vector_width);
    }
    if (target.has_feature(Target::HVX)) {
        f.hexagon();
    }
    f.jit_handlers().custom_error = halide_error;

    // choose values that will fail
    p1.set(1);
    p2.set(2);
    error_occurred = false;
    result = f.realize({realize_width});
    ASSERT_TRUE(error_occurred) << "There should have been a requirement error (vector_width = " << vector_width << ")";

    p1.set(1);
    p2.set(kPrime1 - 1);
    error_occurred = false;
    result = f.realize({realize_width});
    ASSERT_FALSE(error_occurred) << "There should not have been a requirement error (vector_width = " << vector_width << ")";
    for (int i = 0; i < realize_width; ++i) {
        const int32_t expected = (kPrime1 * kPrime2) + i;
        const int32_t actual = result(i);
        ASSERT_EQ(actual, expected) << "Unexpected value at " << i << ": actual=" << actual << ", expected=" << expected << " (vector_width = " << vector_width << ")";
    }

    ImageParam input(Int(32), 2);
    Expr h = require(p1 == p2, p1);
    Func clamped = BoundaryConditions::repeat_edge(input, {{0, 64}, {0, h}});
    clamped.jit_handlers().custom_error = &halide_error;

    Buffer<int32_t> input_buf(64, 64);
    input_buf.fill(0);
    input.set(input_buf);
    p1.set(16);
    p2.set(15);

    error_occurred = false;
    result = clamped.realize({64, 3});
    ASSERT_TRUE(error_occurred) << "There should have been a requirement error (vector_width = " << vector_width << ")";

    p1.set(16);
    p2.set(16);

    error_occurred = false;
    result = clamped.realize({64, 3});
    ASSERT_FALSE(error_occurred) << "There should NOT have been a requirement error (vector_width = " << vector_width << ")";
}

INSTANTIATE_TEST_SUITE_P(
    VectorWidths,
    RequireTest,
    ::testing::Values(0, 4, 32));
