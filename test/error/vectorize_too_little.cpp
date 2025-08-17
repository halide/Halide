#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestVectorizeTooLittle() {
    Var x, y;

    Buffer<int> input(5, 5);
    Func f;
    f(x, y) = input(x, y) * 2;
    f.vectorize(x, 0);

    // Should result in an error
    Buffer<int> out = f.realize({5, 5});
}
}  // namespace

TEST(ErrorTests, VectorizeTooLittle) {
    EXPECT_COMPILE_ERROR(TestVectorizeTooLittle, HasSubstr("TODO"));
}
