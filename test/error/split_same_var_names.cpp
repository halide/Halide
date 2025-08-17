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
    EXPECT_COMPILE_ERROR(
        TestSplitSameVarNames,
        MatchesPattern(R"(In schedule for f\d+, can't split v\d+ into v\d+ and v\d+ )"
                       R"(because the new Vars have the same name\.\n)"
                       R"(Vars: v\d+ __outermost)"));
}
