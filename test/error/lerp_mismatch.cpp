#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestLerpMismatch() {
    // This should trigger an error.
    Func f;
    f() = lerp(cast<uint16_t>(0), cast<uint8_t>(42), 0.5f);
}
}  // namespace

TEST(ErrorTests, LerpMismatch) {
    EXPECT_COMPILE_ERROR(
        TestLerpMismatch,
        HasSubstr("Can't lerp between (uint16)0 of type uint16 and "
                  "(uint8)42 of different type uint8"));
}
