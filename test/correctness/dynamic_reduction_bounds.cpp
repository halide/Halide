#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(DynamicReductionBoundsTest, Basic) {
    ImageParam input(Float(32), 2);

    Var x, y, z;
    RDom dom(0, input.width() * 8);
    Func f;
    Expr hard_to_reason_about = cast<int>(hypot(input.width(), input.height()));
    f(x, y, z) = 1;
    f(x, y, dom / hard_to_reason_about) += 1;
    f.compile_jit();

    Buffer<float> im(32, 32);
    input.set(im);

    f.realize({100, 100, 16});
}
