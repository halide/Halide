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
    EXPECT_COMPILE_ERROR(TestShiftInwardsAndBlendRace, HasSubstr("TODO"));
}
