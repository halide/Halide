#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestCallableBadArguments() {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Callable c = f.compile_to_callable({p_img, p_float});
}
}  // namespace

TEST(ErrorTests, CallableBadArguments) {
    EXPECT_COMPILE_ERROR(TestCallableBadArguments, MatchesPattern(R"(Generated code refers to parameter p\d+, which was not found in the argument list\.\n\nArgument list specified: __user_context p\d+ p\d+ \n\nParameters referenced in generated code: p\d+ p\d+ p\d+ \n)"));
}
