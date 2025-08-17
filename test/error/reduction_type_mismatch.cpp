#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestReductionTypeMismatch() {
    Var x;
    Func f;
    RDom dom(0, 50);

    f(x) = cast<uint8_t>(0);  // The type here...
    f(dom) += 1.0f;           // does not match the type here.

    // Should result in an error
    Buffer<float> result = f.realize({50});
}
}  // namespace

TEST(ErrorTests, ReductionTypeMismatch) {
    EXPECT_COMPILE_ERROR(
        TestReductionTypeMismatch,
        MatchesPattern(R"(In update definition 0 of Func \"f\d+\":\n)"
                       "Tuple element 0 of update definition has type float32, "
                       "but pure definition has type uint8"));
}
