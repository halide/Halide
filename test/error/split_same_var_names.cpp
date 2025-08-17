#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestSplitSameVarNames() {
    Var x;
    Func f;
    f(x) = x;
    f.split(x, x, x, 16, TailStrategy::RoundUp);
}
}  // namespace

TEST(ErrorTests, SplitSameVarNames) {
    EXPECT_COMPILE_ERROR(TestSplitSameVarNames, HasSubstr("TODO"));
}
