#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestCallableTypedBadArguments() {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Buffer<uint8_t> in1(10, 10), result1(10, 10);
    in1.fill(0);

    // Should fail with "Error defining 'f': Argument 1 of 4 ('p_int') was expected to be a scalar of type 'int32'."
    auto c = f.compile_to_callable({p_int, p_float, p_img})
                 .make_std_function<Buffer<uint8_t>, uint8_t, float, Buffer<uint8_t>>();
}
}  // namespace

TEST(ErrorTests, CallableTypedBadArguments) {
    EXPECT_RUNTIME_ERROR(
        TestCallableTypedBadArguments,
        MatchesPattern(R"(Error defining 'f(_\d+)?': Argument 1 of 4 \('p\d+'\) )"
                       R"(was expected to be a scalar of type 'int32' and )"
                       R"(dimension 0\.)"));
}
