#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

template<typename>
class DivByZeroTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int8_t, int16_t, int32_t, uint8_t, uint16_t, uint32_t>;
TYPED_TEST_SUITE(DivByZeroTest, IntTypes);

}  // namespace

TYPED_TEST(DivByZeroTest, SimplifierAndCodegen) {
    using T = TypeParam;
    // Division by zero in Halide is defined to return zero, and
    // division by the most negative integer by -1 returns the most
    // negative integer. To preserve the Euclidean identity, this
    // means that x % 0 == x.

    Type t = halide_type_of<T>();

    // First test that the simplifier knows this:
    Expr zero = cast<T>(0);
    Expr x = Variable::make(t, unique_name('t'));

    Expr e = simplify(x / zero == zero);
    ASSERT_TRUE(is_const_one(e)) << e;

    e = simplify(x % zero == zero);
    ASSERT_TRUE(is_const_one(e)) << e;

    if (t.is_int() && t.bits() < 32) {
        e = simplify(t.min() / cast<T>(-1) == t.min());
        ASSERT_TRUE(is_const_one(e)) << simplify(t.min() / cast<T>(-1)) << " vs " << t.min();
        e = simplify(t.min() % cast<T>(-1) == zero);
        ASSERT_TRUE(is_const_one(e)) << e;
    }

    // Now check that codegen does the right thing:
    Param<T> a, b;
    a.set(T{5});
    b.set(T{0});
    T result = evaluate<T>(a / b);
    EXPECT_EQ(result, T{0});
    result = evaluate<T>(a % b);
    EXPECT_EQ(result, T{0});
    if (t.is_int() && t.bits() < 32) {
        uint64_t bits = 1;
        bits <<= (t.bits() - 1);
        T min_val;
        memcpy(&min_val, &bits, sizeof(min_val));
        a.set(min_val);
        b.set(T(-1));
        result = evaluate<T>(a / b);
        EXPECT_EQ(result, min_val);
        result = evaluate<T>(a % b);
        EXPECT_EQ(result, T{0});
    }
}

TEST(DivByZeroTest, ShiftInwardsOverCompute) {
    // Here's a case that illustrates why it's important to have
    // defined behavior for division by zero:

    Func f;
    Var x;
    f(x) = 256 / (x + 1);
    f.vectorize(x, 8, TailStrategy::ShiftInwards);

    // Ignoring scheduling, we're only realizing f over positive
    // values of x, so this shouldn't fault. However scheduling can
    // over-compute. In this case, vectorization with ShiftInwards
    // results in evaluating smaller values of x, including zero. This
    // would fault at runtime if we didn't have defined behavior for
    // division by zero.

    EXPECT_NO_THROW(f.realize({5}));
}
