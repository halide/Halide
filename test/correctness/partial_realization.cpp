#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(PartialRealizationTest, ScatteredHistogram) {
    // Test situations where the args to realize specify a size that's
    // too small to realize into, due to scattering or scheduling.

    Func im;
    Var x, y;
    im(x, y) = x + y;

    Func hist;
    RDom r(0, 100, 0, 100);
    hist(im(r.x, r.y)) += 1;

    // Hist can only be realized over 256 values, so if we ask for
    // less we get a cropped view.
    Buffer<int> h;
    ASSERT_NO_THROW(h = hist.realize({100}));
    for (int i = 0; i < 100; i++) {
        // There's one zero at the top left corner, two ones, three twos, etc.
        int correct = i + 1;
        EXPECT_EQ(h(i), correct) << "i = " << i;
    }
}

TEST(PartialRealizationTest, TiledSchedule) {
    Func f;
    Var x, y;
    f(x, y) = x + y;

    Var xi, yi;
    f.tile(x, y, xi, yi, 16, 8, TailStrategy::RoundUp);

    Buffer<int> buf;
    ASSERT_NO_THROW(buf = f.realize({30, 20}));

    // There's no way to realize over that domain with the given
    // schedule. Instead Halide has realized a 32x24 buffer and
    // returned a crop of it.
    EXPECT_EQ(buf.dim(0).extent(), 30) << "Incorrect width";
    EXPECT_EQ(buf.dim(1).extent(), 20) << "Incorrect height";

    EXPECT_EQ(buf.dim(0).stride(), 1) << "Incorrect x stride";
    EXPECT_EQ(buf.dim(1).stride(), 32) << "Incorrect y stride";

    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 30; x++) {
            int correct = x + y;
            EXPECT_EQ(buf(x, y), correct) << "buf(" << x << ", " << y << ") = " << buf(x, y) << " instead of " << correct;
        }
    }
}
