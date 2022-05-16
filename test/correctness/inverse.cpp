#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int bits_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    uint32_t a_exp = a >> 23;
    uint32_t b_exp = b >> 23;
    if (a_exp != b_exp) return -100;
    uint32_t diff = a > b ? a - b : b - a;
    int count = 0;
    while (diff) {
        count++;
        diff /= 2;
    }
    return count;
}

// Check the mantissas match except for the last few bits.
void check(Buffer<float> a, Buffer<float> b) {
    for (int i = 0; i < a.width(); i++) {
        int err = bits_diff(a(i), b(i));
        if (err > 13) {
            printf("Mismatch in mantissa at %d: %10.10f %10.10f. Differs by %d bits.\n", i, a(i), b(i), err);
            // exit(-1);
        }
    }
}

int main(int argc, char **argv) {

    Func f1, f2, f3, f4, f5;
    Func g1, g2, g3, g4, g5;

    Var x, xi;
    Expr v = x * 1.34f + 1.0142f;

    Param<float> p;
    p.set(1.0f);

    // Test accuracy of reciprocals.

    // First prevent any optimizations by hiding 1.0 in a param.
    f1(x) = p / v;

    // Now test various vectorization widths with an explicit 1.0. On
    // arm 2 and 4 trigger optimizations. On x86 4 and 8 do.
    f2(x) = fast_inverse(v);
    f2.vectorize(x, 2);

    f3(x) = fast_inverse(v);
    f3.vectorize(x, 4);

    f4(x) = fast_inverse(v);
    f4.vectorize(x, 8);

    // Same thing for reciprocal square root.
    g1(x) = p / sqrt(v);

    g2(x) = fast_inverse_sqrt(v);
    g2.vectorize(x, 2);

    g3(x) = fast_inverse_sqrt(v);
    g3.vectorize(x, 4);

    g4(x) = fast_inverse_sqrt(v);
    g4.vectorize(x, 8);

    // Also test both on the GPU.
    f5(x) = fast_inverse(v);
    g5(x) = fast_inverse_sqrt(v);

    Target t = get_jit_target_from_environment();
    if (t.has_gpu_feature()) {
        f5.gpu_tile(x, xi, 16);
        g5.gpu_tile(x, xi, 16);
    }

    Buffer<float> imf1 = f1.realize({10000});
    Buffer<float> imf2 = f2.realize({10000});
    Buffer<float> imf3 = f3.realize({10000});
    Buffer<float> imf4 = f4.realize({10000});
    Buffer<float> imf5 = f5.realize({10000});

    Buffer<float> img1 = g1.realize({10000});
    Buffer<float> img2 = g2.realize({10000});
    Buffer<float> img3 = g3.realize({10000});
    Buffer<float> img4 = g4.realize({10000});
    Buffer<float> img5 = g5.realize({10000});

    printf("Testing accuracy of inverse\n");
    check(imf1, imf2);
    check(imf1, imf3);
    check(imf1, imf4);
    check(imf1, imf5);
    printf("Pass.\n");
    printf("Testing accuracy of inverse sqrt\n");
    check(img1, img2);
    check(img1, img3);
    check(img1, img4);
    check(img1, img5);
    printf("Pass.\n");

    printf("Success!\n");
    return 0;
}
