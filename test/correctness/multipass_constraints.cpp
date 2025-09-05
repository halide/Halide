#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(MultipassConstraintsTest, MultipassConstraints) {
    ImageParam in(Float(32), 2, "in");

    Func out("out");
    Var x("x"), y("y");

    out(x, y) = in(x + 1, y + 1) + in(x - 1, y - 1);
    out(x, y) += 3.0f;
    out.update().vectorize(x, 4);

    OutputImageParam o = out.output_buffer();

    // Now make some hard-to-resolve constraints
    in.dim(0).set_bounds(in.dim(1).min() - 5, in.dim(1).extent() + o.dim(0).extent());

    o.dim(0).set_bounds(0, select(o.dim(0).extent() < 22, o.dim(0).extent() + 1, o.dim(0).extent()));

    // Make a bounds query buffer
    Buffer<float> out_buf(nullptr, 7, 8);
    out_buf.set_min(2, 2);

    ASSERT_NO_THROW(out.infer_input_bounds(out_buf));

    EXPECT_EQ(in.get().dim(0).min(), -4);
    EXPECT_EQ(in.get().dim(0).extent(), 34);
    EXPECT_EQ(in.get().dim(1).min(), 1);
    EXPECT_EQ(in.get().dim(1).extent(), 10);
    EXPECT_EQ(out_buf.dim(0).min(), 0);
    EXPECT_EQ(out_buf.dim(0).extent(), 24);
    EXPECT_EQ(out_buf.dim(1).min(), 2);
    EXPECT_EQ(out_buf.dim(1).extent(), 8);
}
