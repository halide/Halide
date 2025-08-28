#include "Halide.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace Halide;

namespace {
int bits_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    uint32_t a_exp = a >> 23;
    uint32_t b_exp = b >> 23;
    if (a_exp != b_exp) {
        return -100;
    }
    uint32_t diff = a > b ? a - b : b - a;
    int count = 0;
    while (diff) {
        count++;
        diff >>= 1;
    }
    return count;
}
}  // namespace

TEST(ErfTest, AccuracyWithinFourBits) {
    Func f;
    Var x;

    f(x) = erf((x - 50000) / 10000.0f);
    f.vectorize(x, 8);

    Buffer<float> im = f.realize({100000});

    int max_err = 0;
    float max_err_x = 0;
    for (int i = 0; i < 100000; i++) {
        float xv = (i - 50000) / 10000.0f;
        float correct = erff(xv);
        float approx = im(i);
        int err = bits_diff(correct, approx);
        if (err > max_err) {
            max_err = err;
            max_err_x = xv;
        }
    }

    EXPECT_LE(max_err, 4) << "Maximum incorrect mantissa bits = " << max_err << " at x=" << max_err_x;
}
