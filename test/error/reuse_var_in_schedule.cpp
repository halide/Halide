#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestReuseVarInSchedule() {
    Func f;
    Var x;

    f(x) = x;

    Var xo, xi;
    f.split(x, xo, xi, 4).split(xo, xo, xi, 4);
}
}  // namespace

TEST(ErrorTests, ReuseVarInSchedule) {
    EXPECT_COMPILE_ERROR(
        TestReuseVarInSchedule,
        MatchesPattern(R"(In schedule for f\d+, can't create var v\d+ using a split )"
                       R"(or tile, because v\d+ is already used in this Func's )"
                       R"(schedule elsewhere\.\n)"
                       R"(Vars: v\d+\.v\d+ v\d+\.v\d+ __outermost)"));
}
