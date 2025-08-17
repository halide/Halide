#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestCallableBadValuesPassed() {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Buffer<uint8_t> in1(10, 10), result1(10, 10);
    in1.fill(0);

    Callable c = f.compile_to_callable({p_img, p_int, p_float});

    // Should fail with something like "Argument 2 of 4 ('p_int') was expected to be a scalar of type 'int32'."
    c(in1, 3.1415927, 1.0f, result1);
}
}  // namespace

TEST(ErrorTests, CallableBadValuesPassed) {
    EXPECT_RUNTIME_ERROR(
        TestCallableBadValuesPassed,
        MatchesPattern(R"(Error calling 'f_\d+': Argument 2 of 4 \('p\d+'\) )"
                       R"(was expected to be a scalar of type 'int32' and )"
                       R"(dimension 0\.)"));
}
