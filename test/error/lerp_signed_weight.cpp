#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestLerpSignedWeight() {
    // This should trigger an error.
    Func f;
    f() = lerp(cast<uint8_t>(0), cast<uint8_t>(42), cast<int8_t>(16));
}
}  // namespace

TEST(ErrorTests, LerpSignedWeight) {
    EXPECT_COMPILE_ERROR(TestLerpSignedWeight, MatchesPattern(R"(A lerp weight must be an unsigned integer or a float, but lerp weight \(int\d+\)16 has type int\d+\.)"));
}
