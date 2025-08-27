#include "Halide.h"
#include <gtest/gtest.h>
#include <stdio.h>

using namespace Halide;

namespace {
template<typename T>
bool test_type() {
    Type t = type_of<T>();
    Func f;
    Var x;
    f(x) = cast<T>(1);
    Buffer<T> im = f.realize({10});

    if (f.value().type() != t) {
        std::cout << "Function was defined with type " << t << " but has type " << f.value().type() << "\n";
        return false;
    }

    Expr add_one = im(_) + 1;
    if (add_one.type() != t) {
        std::cout << "Add 1 changed type from " << t << " to " << add_one.type() << "\n";
        return false;
    }

    Expr one_add = 1 + im(_);
    if (one_add.type() != t) {
        std::cout << "Pre-add 1 changed type from " << t << " to " << one_add.type() << "\n";
        return false;
    }

    /*
      The following will indeed change the type, because we don't do early constant folding
    Expr add_exp = im() + (Expr(1) + 1);
    if (add_exp.type() != t) {
        std::cout << "Add constant expression changed type from " << t << " to " << add_exp.type() << "\n";
    }
    */

    return true;
}
}  // namespace

TEST(ConstantTypeTest, TypePreservation) {
    EXPECT_TRUE(test_type<uint8_t>()) << "Type check failed for uint8_t";
    EXPECT_TRUE(test_type<uint16_t>()) << "Type check failed for uint16_t";
    EXPECT_TRUE(test_type<uint32_t>()) << "Type check failed for uint32_t";
    EXPECT_TRUE(test_type<int8_t>()) << "Type check failed for int8_t";
    EXPECT_TRUE(test_type<int16_t>()) << "Type check failed for int16_t";
    EXPECT_TRUE(test_type<int32_t>()) << "Type check failed for int32_t";
    EXPECT_TRUE(test_type<float>()) << "Type check failed for float";
    EXPECT_TRUE(test_type<double>()) << "Type check failed for double";
}
