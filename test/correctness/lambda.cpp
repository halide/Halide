#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(LambdaTest, BasicLambdaAndImplicitArgs) {
    Func f;
    Var x, y, z;
    f(x, y) = x * y;

    // g is equivalent to f above - a two argument function that
    // returns the product of the arguments
    Func g = lambda(x, y, x * y);

    // Use lambdas and implicit args in the one line
    Buffer<int32_t> im = lambda(f(_) - g(_) + lambda(x, y, x + y)(_)).realize({10, 10});

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int correct = x + y;
            EXPECT_EQ(im(x, y), correct) << "im(" << x << ", " << y << ")";
        }
    }
}

TEST(LambdaTest, ImplicitArgsInLambda) {
    Func h;
    Var x, y, z;
    h(x, y, z) = x + y * y + z * z * z;  // Ordering of arguments affects results

    Buffer<int32_t> im2 = lambda(_, z, h(_, z)).realize({10, 10, 10});

    for (int z = 0; z < 10; z++) {
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                int correct = x + y * y + z * z * z;
                EXPECT_EQ(im2(x, y, z), correct) << "im2(" << x << ", " << y << ", " << z << ")";
            }
        }
    }
}
