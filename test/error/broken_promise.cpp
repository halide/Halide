#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBrokenPromise() {
    Buffer<uint16_t> ten_bit_data(100);
    for (int i = 0; i < 100; i++) {
        ten_bit_data(i) = i * 20;
    }

    Buffer<float> ten_bit_lut(1024);

    for (int i = 0; i < 1024; i++) {
        ten_bit_lut(i) = sin(2 * 3.1415f * i / 1024.0f);
    }

    Var x;
    Func f;
    ImageParam in(UInt(16), 1);
    ImageParam lut(Float(32), 1);

    f(x) = lut(unsafe_promise_clamped(in(x), 0, 1023));
    lut.dim(0).set_bounds(0, 1024);

    in.set(ten_bit_data);
    lut.set(ten_bit_lut);

    auto result = f.realize({100}, get_jit_target_from_environment().with_feature(Target::CheckUnsafePromises));
}
}  // namespace

TEST(ErrorTests, BrokenPromise) {
    EXPECT_RUNTIME_ERROR(
        TestBrokenPromise,
        MatchesPattern(
            R"(Requirement Failed: \(\(\(\(uint16\)p\d+\[f\d+\.s\d+\.v\d+ )"
            R"(- p\d+\.min\.0\] >= \(uint16\)0\) && )"
            R"(\(\(uint16\)p\d+\[f\d+\.s\d+\.v\d+ - p\d+\.min\.0\] <= )"
            R"(\(uint16\)1023\)\)\) from unsafe_promise_clamped)"));
}
