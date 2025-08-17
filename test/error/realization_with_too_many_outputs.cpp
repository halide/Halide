#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRealizationWithTooManyOutputs() {
    Func f;
    Var x;
    f(x) = 42;

    // Should result in an error
    Buffer<int> first(10);
    Buffer<int> second(10);

    Realization r({first, second});
    f.realize(r);
}
}  // namespace

TEST(ErrorTests, RealizationWithTooManyOutputs) {
    EXPECT_COMPILE_ERROR(
        TestRealizationWithTooManyOutputs,
        HasSubstr("Realization requires 2 output(s) but pipeline "
                  "produces 1 result(s)."));
}
