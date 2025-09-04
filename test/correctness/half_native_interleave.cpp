#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(HalfNativeInterleave, WideningOpsVectorization) {
    // Generate random input.
    const int W = 256;
    Buffer<uint8_t> input(W);
    for (int x = 0; x < W; x++) {
        input(x) = rand() & 0xff;
    }

    Var x("x");
    Func input_16("input_16"), product("product"), sum("sum"), diff("difference");
    input_16(x) = cast<int16_t>(input(x));

    product(x) = (input_16(x) * 2);
    sum(x) = (input_16(x) + 2);
    diff(x) = (input_16(x) - 2);

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::HVX)) {
        // Vectorize by one vector width.
        // Since the operations are widening ops,
        // the operands are effectively half-vector width.
        // The assertion referenced in issue below
        // shouldn't be triggered:
        // https://github.com/halide/Halide/issues/1582
        product.hexagon().vectorize(x, 64);
        sum.hexagon().vectorize(x, 64);
        diff.hexagon().vectorize(x, 64);
    } else {
        product.vectorize(x, target.natural_vector_size<uint8_t>());
        sum.vectorize(x, target.natural_vector_size<uint8_t>());
        diff.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Buffer<int16_t> out_p = product.realize({W}, target);
    Buffer<int16_t> out_s = sum.realize({W}, target);
    Buffer<int16_t> out_d = diff.realize({W}, target);

    for (int x = 1; x < W - 1; x++) {
        int16_t correct_p = input(x) * 2;
        int16_t correct_s = input(x) + 2;
        int16_t correct_d = input(x) - 2;

        EXPECT_EQ(out_p(x), correct_p) << "product at x=" << x;
        EXPECT_EQ(out_s(x), correct_s) << "sum at x=" << x;
        EXPECT_EQ(out_d(x), correct_d) << "difference at x=" << x;
    }
}
