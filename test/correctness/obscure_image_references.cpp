#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ObscureImageReferencesTest, Basic) {
    ImageParam im1(UInt(8), 1);
    Buffer<uint8_t> im2(10), im3(20);
    Param<int> j;

    ASSERT_EQ(im1.dimensions(), 1);
    ASSERT_EQ(im2.dimensions(), 1);
    ASSERT_EQ(im3.dimensions(), 1);

    Func f;
    Var x;
    f(x) = x + im1.width();
    RDom r(0, cast<int>(clamp(im2(j), 0, 99)));
    f(r) = 37;

    im2(3) = 10;

    j.set(3);
    im1.set(im3);
    Buffer<int> result = f.realize({100});

    for (int i = 0; i < 100; i++) {
        int correct = i < im2(3) ? 37 : (i + 20);
        EXPECT_EQ(result(i), correct) << "result(" << i << ") = " << result(i) << " instead of " << correct;
    }
}
