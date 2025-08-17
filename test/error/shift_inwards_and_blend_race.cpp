#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestShiftInwardsAndBlendRace() {
    Func f;
    Var x;

    f(x) = 0;
    f(x) += 4;

    // This schedule should be forbidden, because it causes a race condition.
    f.update().vectorize(x, 8, TailStrategy::ShiftInwardsAndBlend).parallel(x);
}
}  // namespace

TEST(ErrorTests, ShiftInwardsAndBlendRace) {
    EXPECT_COMPILE_ERROR(
        TestShiftInwardsAndBlendRace,
        MatchesPattern(R"(Tail strategy ShiftInwardsAndBlend may not be used to )"
                       R"(split v\d+ because other vars stemming from the same )"
                       R"(original Var or RVar are marked as parallel\.)"
                       R"(This could cause a race condition\.)"));
}
