// Test whether min[] and extent[] of an ImageParam are correctly passed into
// the filter.
#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(MinExtentTest, MinExtent) {
    Var x("x");
    Func f("f");
    ImageParam in(Int(32), 1, "in");
    Expr factor("factor");

    // Multiply by -10 or +10 for coordinates that fall outside the input
    // image.
    factor = select(x < in.left(), -10,
                    select(x > in.right(), 10, 1));
    f(x) = factor * x;

    // Create input and output buffers. The input pixels are never accessed,
    // but we initialize them anyway.
    Buffer<int> input(5);
    Buffer<int> out(10);
    input.fill(0);
    out.fill(0);

    // Change coordinate origin of input and output buffer so that they are
    // aligned as follows:
    // input         |------|
    // out     |-----------------|
    const int INOFF = 4;
    const int OUTOFF = 1;
    input.set_min(INOFF);
    out.set_min(OUTOFF);
    in.set(input);
    
    ASSERT_NO_THROW(f.realize(out));

    int expected[] = {-10, -20, -30, 4, 5, 6, 7, 8, 90, 100};
    for (int i = 0; i < out.width(); i++) {
        EXPECT_EQ(out(i + OUTOFF), expected[i])
            << "Unexpected output at index " << i;
    }
}
