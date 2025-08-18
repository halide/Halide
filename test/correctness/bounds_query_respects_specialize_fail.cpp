#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::ConciseCasts;

TEST(BoundsQueryTest, RespectsSpecializeFail) {
    ImageParam im(UInt(8), 1);
    Func f;
    Var x;

    f(x) = im(x);

    im.dim(0).set_stride(Expr());
    f.specialize(im.dim(0).stride() == 1);
    f.specialize(im.dim(0).stride() == 2);
    f.specialize_fail("unreachable");

    Callable c = f.compile_to_callable({im});

    Runtime::Buffer<uint8_t> in_buf(nullptr, {halide_dimension_t{0, 0, 0}});
    Runtime::Buffer<uint8_t> out_buf(32);

    int result = c(in_buf, out_buf);
    EXPECT_EQ(result, 0) << "Callable failed";

    EXPECT_EQ(in_buf.dim(0).stride(), 1) << "Unexpected bounds query result";
    EXPECT_EQ(in_buf.dim(0).extent(), 32) << "Unexpected bounds query result";
}
