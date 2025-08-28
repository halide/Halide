#include "Halide.h"
#include <gtest/gtest.h>
#include <limits>

using namespace Halide;

namespace {

template<typename>
class DivRoundToZeroTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int8_t, int16_t, int32_t>;
TYPED_TEST_SUITE(DivRoundToZeroTest, IntTypes);

}  // namespace

TYPED_TEST(DivRoundToZeroTest, Basic) {
    using T = TypeParam;

    Func f;
    Var x, y;

    Expr d = cast<T>(y - 128);
    Expr n = cast<T>(x - 128);
    d = select(d == 0 || (d == -1 && n == d.type().min()),
               cast<T>(1),
               d);
    f(x, y) = div_round_to_zero(n, d);

    f.vectorize(x, 8);

    Buffer<T> result = f.realize({256, 256});

    for (int dd = -128; dd < 128; dd++) {
        for (int nn = -128; nn < 128; nn++) {
            if (dd == -1 && nn == std::numeric_limits<T>::min()) {
                continue;
            }
            EXPECT_EQ(result(nn + 128, dd + 128), dd == 0 ? nn : static_cast<T>(nn / dd))
                << "result(" << nn << ", " << dd << ")";
        }
    }
}

TYPED_TEST(DivRoundToZeroTest, Fast) {
    using T = TypeParam;

    Func f;
    Var x, y;
    f(x, y) = fast_integer_divide_round_to_zero(cast<T>(x - 128), cast<uint8_t>(y + 1));
    f.vectorize(x, 8);
    Buffer<T> result_fast = f.realize({256, 255});
    for (int dd = 1; dd < 256; dd++) {
        for (int nn = -128; nn < 128; nn++) {
            int correct = static_cast<T>(nn / dd);
            int r = result_fast(nn + 128, dd - 1);
            EXPECT_EQ(r, correct) << "result_fast(" << nn << ", " << dd << ")";
        }
    }
}

TYPED_TEST(DivRoundToZeroTest, ConstantDenominators) {
    using T = TypeParam;

    for (int d : {-128, -54, -3, -1, 1, 2, 25, 32, 127}) {
        Func f;
        Var x;
        f(x) = div_round_to_zero(cast<T>(x - 128), cast<T>(d));
        f.vectorize(x, 8);
        Buffer<T> result_const = f.realize({256});
        for (int n = -128; n < 128; n++) {
            int correct = static_cast<T>(n / d);
            int r = result_const(n + 128);
            EXPECT_EQ(r, correct) << "result_const(" << n << ", " << d << ")";
        }
    }
}
