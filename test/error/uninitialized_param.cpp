#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUninitializedParam() {
    ImageParam image_param(Int(32), 2, "image_param");
    Param<int> scalar_param("scalar_param");

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = image_param(x, y) + scalar_param;

    Buffer<int> b(10, 10);
    image_param.set(b);

    f.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, UninitializedParam) {
    EXPECT_COMPILE_ERROR(
        TestUninitializedParam,
        HasSubstr("Parameter scalar_param does not have a valid scalar value."));
}
