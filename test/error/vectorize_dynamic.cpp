#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestVectorizeDynamic() {
    Var x, y;

    Buffer<int> input(5, 5);
    Func f;
    f(x, y) = input(x, y) * 2;
    Var xo, xi;

    Param<int> vector_size;

    // You can only vectorize across compile-time-constant sizes.
    f.split(x, xo, xi, vector_size).vectorize(xi);

    // Should result in an error
    vector_size.set(4);
    Buffer<int> out = f.realize({5, 5});
}
}  // namespace

TEST(ErrorTests, VectorizeDynamic) {
    EXPECT_COMPILE_ERROR(TestVectorizeDynamic, MatchesPattern(R"(Can only vectorize for loops over a constant extent\.\nLoop over f\d+\.s\d+\.v\d+\.v\d+ has extent p\d+\.)"));
}
