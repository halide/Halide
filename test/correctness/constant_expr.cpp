#include "Halide.h"
#include <gtest/gtest.h>
#include <limits>

#ifdef _MSC_VER
#pragma warning(disable : 4800)  // forcing value to bool 'true' or 'false'
#endif

using namespace Halide;
using namespace Halide::Internal;

namespace {

template<typename T>
bool scalar_from_constant_expr(Expr e, T *val) {
    if (type_of<T>().is_int()) {
        auto i = as_const_int(e);
        if (!i) return false;
        *val = (T)(*i);
        return true;
    } else if (type_of<T>().is_uint()) {
        auto u = as_const_uint(e);
        if (!u) return false;
        *val = (T)(*u);
        return true;
    } else if (type_of<T>().is_float()) {
        auto f = as_const_float(e);
        if (!f) return false;
        *val = (T)(*f);
        return true;
    } else {
        return false;
    }
}

template<typename T>
void test_expr(T value) {
    Type t = type_of<T>();
    Expr e = make_const(t, value);
    EXPECT_EQ(e.type(), t) << "constant of type " << t << " returned expr of type " << e.type();
    T nvalue{0};
    ASSERT_TRUE(scalar_from_constant_expr<T>(e, &nvalue)) << "constant of type " << t << " failed scalar_from_constant_expr with value " << value;
    EXPECT_EQ(nvalue, value) << "Roundtrip failed for type " << t << ": input " << value << " output " << nvalue;
}

}  // anonymous namespace

template<typename>
class ConstantExprTypeTest : public ::testing::Test {};

using ConstantTypes = ::testing::Types<bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, double>;
TYPED_TEST_SUITE(ConstantExprTypeTest, ConstantTypes);

TYPED_TEST(ConstantExprTypeTest, Range) {
    using T = TypeParam;
    const T low = std::numeric_limits<T>::lowest();
    const T min = std::numeric_limits<T>::min();
    const T max = std::numeric_limits<T>::max();

    test_expr<T>(0);
    test_expr<T>(1);

    test_expr<T>(low);
    test_expr<T>(min);
    test_expr<T>(max);
}

// Test various edge cases for int64 and double, since we do extra voodoo to
// disassemble and reassemble them.
TEST(ConstantExprTest, Int64EdgeCases) {
    test_expr<int64_t>(-64);
    test_expr<int64_t>((int64_t)0x000000007fffffff);
    test_expr<int64_t>((int64_t)0x0000000080000000);
    test_expr<int64_t>((int64_t)0x0000000080000001);
    test_expr<int64_t>((int64_t)0x00000000ffffffff);
    test_expr<int64_t>((int64_t)0x00000001ffffffff);
    test_expr<int64_t>((int64_t)0x7fffffff00000000);
    test_expr<int64_t>((int64_t)0x7fffffff80000000);
    test_expr<int64_t>((int64_t)0xffffffff80000000);
    test_expr<int64_t>((int64_t)0xffffffff00000001);
    test_expr<int64_t>((int64_t)0x7FFFFFFFFFFFFFFF);
    test_expr<int64_t>((int64_t)0x8000000000000000);
    test_expr<int64_t>((int64_t)0x8000000000000001);
}

TEST(ConstantExprTest, UInt64EdgeCases) {
    test_expr<uint64_t>(-64);
    test_expr<uint64_t>((uint64_t)0x000000007fffffff);
    test_expr<uint64_t>((uint64_t)0x0000000080000000);
    test_expr<uint64_t>((uint64_t)0x0000000080000001);
    test_expr<uint64_t>((uint64_t)0x00000000ffffffff);
    test_expr<uint64_t>((uint64_t)0x00000001ffffffff);
    test_expr<uint64_t>((uint64_t)0x7fffffff00000000);
    test_expr<uint64_t>((uint64_t)0x7fffffff80000000);
    test_expr<uint64_t>((uint64_t)0xffffffff80000000);
    test_expr<uint64_t>((uint64_t)0xffffffff00000001);
    test_expr<uint64_t>((uint64_t)0x7FFFFFFFFFFFFFFF);
    test_expr<uint64_t>((uint64_t)0x8000000000000000);
    test_expr<uint64_t>((uint64_t)0x8000000000000001);
}

TEST(ConstantExprTest, FloatEdgeCases) {
    test_expr<float>(3.141592f);
    test_expr<float>(3.40282e+38f);
    test_expr<float>(3.40282e+38f);
}

TEST(ConstantExprTest, DoubleEdgeCases) {
    test_expr<double>(3.1415926535897932384626433832795);
    test_expr<double>(1.79769e+308);
    test_expr<double>(-1.79769e+308);
}
