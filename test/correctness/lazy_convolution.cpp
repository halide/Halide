#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int call_count;
extern "C" HALIDE_EXPORT_SYMBOL float call_counter(float x) {
    call_count++;
    return x;
}
HalidePureExtern_1(float, call_counter, float);
}  // namespace

TEST(LazyConvolutionTest, ConditionalConvolution) {
    Func f;
    Var x, y;
    f(x, y) = call_counter(sin(x * 3 + y));

    // f contains values in [-1, 1]. Now compute a convolution over f
    // only where f is positive. If f is negative, we'll skip the work
    // and write a zero instead.
    Func blur;
    RDom r(-10, 20, -10, 20);
    blur(x, y) = select(f(x, y) > 0, sum(f(x + r.x, y + r.y)), 0);

    call_count = 0;
    blur.realize({100, 100});

    // If we computed the convolution everywhere, call_count would be
    // 100*100*20*20. Because we only compute it in half of the
    // places, it should be smaller; roughly 100*100*20*20*0.5.
    EXPECT_LT(call_count, 2100000) << "Expected call_count ~= 2000000";
}
