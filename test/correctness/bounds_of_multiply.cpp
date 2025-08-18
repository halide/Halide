#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// See https://github.com/halide/Halide/issues/3070
namespace {
template<typename T>
void test() {
    Param<T> bound;
    ImageParam in(UInt(8), 1);
    Var x;
    Func f;

    f(x) = in(clamp(x, 0, bound * 2 - 1));

    Buffer<uint8_t> foo(10);
    foo.fill(0);
    in.set(foo);
    bound.set(5);

    auto result = f.realize({200});
}
}  // namespace

TEST(BoundsTest, MultiplyInClampInt32) {
    test<int32_t>();
}

TEST(BoundsTest, MultiplyInClampInt16) {
    test<int16_t>();
}
