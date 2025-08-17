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
    EXPECT_COMPILE_ERROR(TestLerpMismatch, MatchesPattern(R"(Can't lerp between \(uint\d+\)0 of type uint\d+ and \(uint\d+\)42 of different type uint\d+)"));
}
