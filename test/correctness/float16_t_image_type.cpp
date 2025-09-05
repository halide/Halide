#include "Halide.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace Halide;

namespace {
template<typename T>
bool test() {
    Halide::Buffer<T> im(10, 3);
    im.set_min(4, -6);

    // Write a constant value and check we can read it back. Mostly
    // this checks the addressing math is doing the right thing for
    // float16_t.
    im.for_each_element([&](int x, int y) {
        im(x, y) = T(x + y / 8.0);
    });

    if (im.size_in_bytes() != im.number_of_elements() * 2) {
        printf("Incorrect amount of memory allocated\n");
        return false;
    }

    for (int y = im.dim(1).min(); y <= im.dim(1).max(); y++) {
        for (int x = im.dim(0).min(); x <= im.dim(0).max(); x++) {
            float correct = x + y / 8.0f;
            float actual = (float)im(x, y);
            if (correct != actual) {
                printf("im(%d, %d) = %f instead of %f\n",
                       x, y, actual, correct);
                return false;
            }
        }
    }

    return true;
}
}

TEST(Float16tImageTypeTest, Float16t) {
    EXPECT_TRUE(test<float16_t>());
}

TEST(Float16tImageTypeTest, Bfloat16t) {
    EXPECT_TRUE(test<bfloat16_t>());
}
