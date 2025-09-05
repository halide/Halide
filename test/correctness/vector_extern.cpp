#include "Halide.h"
#include <gtest/gtest.h>
#include <math.h>

using namespace Halide;

TEST(VectorExternTest, Basic) {
    Var x, y;
    Func f, g;

    f(x) = sqrt(cast<float>(x));

    f.vectorize(x, 4);
    Buffer<float> im = f.realize({32});

    for (int i = 0; i < 32; i++) {
        float correct = sqrtf((float)i);
        EXPECT_NEAR(im(i), correct, 0.001f) << "im(" << i << ") = " << im(i) << " instead of " << correct;
    }
}
