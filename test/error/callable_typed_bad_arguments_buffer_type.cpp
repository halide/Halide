#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestCallableTypedBadArgumentsBufferType() {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Buffer<float> in1(10, 10), result1(10, 10);
    in1.fill(0);

    // Should fail with "Error defining 'f': Argument 1 of 4 ('p_img') was expected to be a buffer of type 'uint8' and dimension 2."
    auto c = f.compile_to_callable({p_img, p_int, p_float})
                 .make_std_function<Buffer<float, 2>, int32_t, float, Buffer<float, 2>>();
}
}  // namespace

TEST(ErrorTests, CallableTypedBadArgumentsBufferType) {
    EXPECT_RUNTIME_ERROR(TestCallableTypedBadArgumentsBufferType, MatchesPattern(R"(Error defining 'f_\d+': Argument 1 of 4 \('p\d+'\) was expected to be a buffer of type 'uint\d+' and dimension 2\.)"));
}
