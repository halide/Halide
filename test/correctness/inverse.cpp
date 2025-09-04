#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
uint32_t absd(uint32_t a, uint32_t b) {
    return a > b ? a - b : b - a;
}

uint32_t ulp_distance(float fa, float fb) {
    if (fa == fb) {
        return 0;  // signed zero
    }
    uint32_t ua = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t ub = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    auto to_ordered = [](uint32_t u) -> uint32_t {
        return u & 0x80000000u ? 0x80000000u - u : u + 0x80000000u;
    };
    return absd(to_ordered(ua), to_ordered(ub));
}

// Check the mantissas match except for the last few bits.
void check(Buffer<float> approx, Buffer<float> exact) {
    for (int i = 0; i < approx.width(); i++) {
        ASSERT_LE(ulp_distance(approx(i), exact(i)), 1u << 8)
            << "Mismatch in mantissa at i = " << i << ": " << approx(i) << " != " << exact(i);
    }
}

class InverseTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};

    Var x;
    Expr v{x * 1.34f + 1.0142f};

    // Prevent any optimizations by hiding 1.0 in a param.
    Param<float> const_one{1.0f};

    void test_approximation(const Expr &exact, const Expr &approximate) {
        // On ARM, widths 2 and 4 trigger optimizations. On x86, 4 and 8 do.

        Func f1, f2, f3, f4;
        f1(x) = exact;
        f2(x) = approximate;
        f3(x) = approximate;
        f4(x) = approximate;

        f2.vectorize(x, 2);
        f3.vectorize(x, 4);
        f4.vectorize(x, 8);

        Buffer<float> expected = f1.realize({10000});
        check(f2.realize({10000}), expected);
        check(f3.realize({10000}), expected);
        check(f4.realize({10000}), expected);

        if (target.has_gpu_feature()) {
            Var xi;
            Func f_gpu;
            f_gpu(x) = approximate;
            f_gpu.gpu_tile(x, xi, 16);
            check(f_gpu.realize({10000}), expected);
        }
    }
};
}  // namespace

TEST_F(InverseTest, FastReciprocalAccuracy) {
    test_approximation(const_one / v, fast_inverse(v));
}

TEST_F(InverseTest, FastInverseSqrtAccuracy) {
    test_approximation(const_one / sqrt(v), fast_inverse_sqrt(v));
}
