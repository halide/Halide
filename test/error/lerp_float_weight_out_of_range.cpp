#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestLerpFloatWeightOutOfRange() {
    // This should trigger an error.
    Func f;
    f() = lerp(0, 42, 1.5f);
}
}  // namespace

TEST(ErrorTests, LerpFloatWeightOutOfRange) {
    EXPECT_COMPILE_ERROR(
        TestLerpFloatWeightOutOfRange,
        HasSubstr("Floating-point weight for lerp with integer arguments "
                  "is 1.5, which is not in the range [0.0, 1.0]."));
}
