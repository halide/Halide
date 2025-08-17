#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestVectorizeTooMuch() {
    Var x, y;

    Buffer<int> input(5, 5);
    Func f;
    f(x, y) = input(x, y) * 2;
    f.vectorize(x, 8).vectorize(y, 8);

    // Should result in an error
    Buffer<int> out = f.realize({5, 5});
}
}  // namespace

TEST(ErrorTests, VectorizeTooMuch) {
    EXPECT_RUNTIME_ERROR(
        TestVectorizeTooMuch,
        MatchesPattern(R"(Input buffer b\d+ is accessed at -3, which )"
                       R"(is before the min \(0\) in dimension 0)"));
}
