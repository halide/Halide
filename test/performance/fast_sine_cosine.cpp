#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Func sin_f, cos_f, sin_ref, cos_ref;
    Var x;
    Expr t = x / 1000.f;
    const float two_pi = 2.0f * static_cast<float>(M_PI);
    sin_f(x) = fast_sin(-two_pi * t + (1 - t) * two_pi);
    cos_f(x) = fast_cos(-two_pi * t + (1 - t) * two_pi);
    sin_ref(x) = sin(-two_pi * t + (1 - t) * two_pi);
    cos_ref(x) = cos(-two_pi * t + (1 - t) * two_pi);
    sin_f.vectorize(x, 8);
    cos_f.vectorize(x, 8);
    sin_ref.vectorize(x, 8);
    cos_ref.vectorize(x, 8);

    double t1 = 1e6 * benchmark([&]() { sin_f.realize(1000); });
    double t2 = 1e6 * benchmark([&]() { cos_f.realize(1000); });
    double t3 = 1e6 * benchmark([&]() { sin_ref.realize(1000); });
    double t4 = 1e6 * benchmark([&]() { cos_ref.realize(1000); });

    printf("sin: %f ns per pixel\n"
           "fast_sine: %f ns per pixel\n"
           "cosine: %f ns per pixel\n"
           "fast_cosine: %f ns per pixel\n",
           t1, t3, t2, t4);

    if (t1 > 0.5f * t3) {
        printf("fast_sin is not 2x faster than sin\n");
        return -1;
    }

    if (t2 > 0.5f * t4) {
        printf("fast_cos is not 2x faster than cos\n");
        return -1;
    }

    return 0;
}
