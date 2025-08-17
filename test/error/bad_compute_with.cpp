#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadComputeWith() {
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f(x, y) += 2;
    f.update(0).compute_with(f, x);

    f.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadComputeWith) {
    EXPECT_COMPILE_ERROR(TestBadComputeWith, MatchesPattern(R"(Cannot schedule f(\$\d+)?\.update\(0\) to be computed with f(\$\d+)?\.s\d+\.x)"));
}
