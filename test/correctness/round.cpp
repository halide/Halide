#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

enum class Op { Round,
                Floor,
                Ceil,
                Trunc };

constexpr auto get_op_fn(const Op op) -> Expr (*)(Expr) {
    switch (op) {
    case Op::Round:
        return round;
    case Op::Floor:
        return floor;
    case Op::Ceil:
        return ceil;
    case Op::Trunc:
        return trunc;
    }
}

template<typename, Op>
struct Scenario;

template<Op op>
struct Scenario<float, op> {
    using Type = float;
    static constexpr auto fn = get_op_fn(op);
    static constexpr int N = 22;
    static constexpr Type inputdata[N] = {
        -2.6f, -2.5f, -2.3f, -1.5f, -1.0f, -0.5f, -0.49999997f, -0.2f, -0.0f,
        +2.6f, +2.5f, +2.3f, +1.5f, +1.0f, +0.5f, 0.49999997f, +0.2f, +0.0f,
        8388609, -8388609, 16777216, -16777218};
    static constexpr Type expected[N] = {};
};

template<Op op>
struct Scenario<double, op> {
    using Type = double;
    static constexpr auto fn = get_op_fn(op);
    static constexpr int N = 24;
    static constexpr Type inputdata[N] = {
        -2.6, -2.5, -2.3, -1.5, -1.0, -0.5, -0.49999999999999994, -0.2, -0.0,
        +2.6, +2.5, +2.3, +1.5, +1.0, +0.5, 0.49999999999999994, +0.2, +0.0,
        8388609, -8388610, 16777216, -16777218,
        4503599627370497, -4503599627370497};
    static constexpr Type expected[N] = {};
};

template<>
constexpr float Scenario<float, Op::Round>::expected[] = {
    -3.0f, -2.0f, -2.0f, -2.0f, -1.0f, -0.0f, -0.0f, -0.0f, -0.0f,
    +3.0f, +2.0f, +2.0f, +2.0f, +1.0f, +0.0f, 0.0f, +0.0f, +0.0f,
    8388609, -8388609, 16777216, -16777218};

template<>
constexpr double Scenario<double, Op::Round>::expected[] = {
    -3.0, -2.0, -2.0, -2.0, -1.0, -0.0, -0.0, -0.0, -0.0,
    +3.0, +2.0, +2.0, +2.0, +1.0, +0.0, 0.0, +0.0, +0.0,
    8388609, -8388610, 16777216, -16777218,
    4503599627370497, -4503599627370497};

template<>
constexpr float Scenario<float, Op::Floor>::expected[] = {
    -3.0f, -3.0f, -3.0f, -2.0f, -1.0f, -1.0f, -1.0f, -1.0f, -0.0f,
    +2.0f, +2.0f, +2.0f, +1.0f, +1.0f, +0.0f, 0.0f, +0.0f, +0.0f,
    8388609, -8388609, 16777216, -16777218};

template<>
constexpr double Scenario<double, Op::Floor>::expected[] = {
    -3.0, -3.0, -3.0, -2.0, -1.0, -1.0, -1.0, -1.0, -0.0,
    +2.0, +2.0, +2.0, +1.0, +1.0, +0.0, 0.0, +0.0, +0.0,
    8388609, -8388610, 16777216, -16777218,
    4503599627370497, -4503599627370497};

template<>
constexpr float Scenario<float, Op::Ceil>::expected[] = {
    -2.0f, -2.0f, -2.0f, -1.0f, -1.0f, -0.0f, -0.0f, -0.0f, -0.0f,
    +3.0f, +3.0f, +3.0f, +2.0f, +1.0f, +1.0f, 1.0f, +1.0f, +0.0f,
    8388609, -8388609, 16777216, -16777218};

template<>
constexpr double Scenario<double, Op::Ceil>::expected[] = {
    -2.0, -2.0, -2.0, -1.0, -1.0, -0.0, -0.0, -0.0, -0.0,
    +3.0, +3.0, +3.0, +2.0, +1.0, +1.0, 1.0, +1.0, +0.0,
    8388609, -8388610, 16777216, -16777218,
    4503599627370497, -4503599627370497};

template<>
constexpr float Scenario<float, Op::Trunc>::expected[] = {
    -2.0f, -2.0f, -2.0f, -1.0f, -1.0f, -0.0f, -0.0f, -0.0f, -0.0f,
    +2.0f, +2.0f, +2.0f, +1.0f, +1.0f, +0.0f, 0.0f, +0.0f, +0.0f,
    8388609, -8388609, 16777216, -16777218};

template<>
constexpr double Scenario<double, Op::Trunc>::expected[] = {
    -2.0, -2.0, -2.0, -1.0, -1.0, -0.0, -0.0, -0.0, -0.0,
    +2.0, +2.0, +2.0, +1.0, +1.0, +0.0, 0.0, +0.0, +0.0,
    8388609, -8388610, 16777216, -16777218,
    4503599627370497, -4503599627370497};

template<typename TypeParam>
class RoundTest : public ::testing::Test {
protected:
    using T = typename TypeParam::Type;
    Target target{get_jit_target_from_environment()};
    void SetUp() override {
        if (!target.supports_type(type_of<T>())) {
            GTEST_SKIP() << "Target does not support " << type_of<T>();
        }
    }
};

using TestScenarios = ::testing::Types<
    Scenario<float, Op::Round>, Scenario<float, Op::Floor>, Scenario<float, Op::Ceil>, Scenario<float, Op::Trunc>,
    Scenario<double, Op::Round>, Scenario<double, Op::Floor>, Scenario<double, Op::Ceil>, Scenario<double, Op::Trunc>>;
}  // namespace

// clang-format off
// TODO: this is a hack to get around the awful way Google Test names its parameterized tests.
namespace testing::internal {
#define OVERRIDE_TYPE_NAME(ty, op) \
    template<> std::string GetTypeName<::Scenario<ty, ::op>>() { return #ty "," #op; }
OVERRIDE_TYPE_NAME(float, Op::Round)
OVERRIDE_TYPE_NAME(float, Op::Floor)
OVERRIDE_TYPE_NAME(float, Op::Ceil)
OVERRIDE_TYPE_NAME(float, Op::Trunc)
OVERRIDE_TYPE_NAME(double, Op::Round)
OVERRIDE_TYPE_NAME(double, Op::Floor)
OVERRIDE_TYPE_NAME(double, Op::Ceil)
OVERRIDE_TYPE_NAME(double, Op::Trunc)
#undef OVERRIDE_TYPE_NAME
}  // namespace testing::internal
// clang-format on

TYPED_TEST_SUITE(RoundTest, TestScenarios);
TYPED_TEST(RoundTest, Check) {
    using S = TypeParam;
    using T = typename S::Type;
    constexpr auto expected = S::expected;

    Buffer<T> input(S::N);
    for (int i = 0; i < S::N; i++) {
        input(i) = S::inputdata[i];
    }

    for (int vector_width = 1; vector_width <= 8; vector_width *= 2) {
        Func f;
        Var x;
        f(x) = S::fn(input(x));
        if (this->target.has_gpu_feature()) {
            f.gpu_single_thread();
        } else if (vector_width > 1) {
            f.vectorize(x, vector_width);
        }

        Buffer<T> rounded = f.realize({S::N});
        for (int i = 0; i < S::N; i++) {
            EXPECT_EQ(rounded(i), expected[i]) << "input(i) = " << input(i) << "\ni = " << i;
        }
    }
}
