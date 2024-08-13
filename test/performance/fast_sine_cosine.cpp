#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (target.arch == Target::X86 &&
        !target.has_feature(Target::SSE41)) {
        printf("[SKIP] These intrinsics are known to be slow on x86 without sse 4.1.\n");
        return 0;
    }

    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Func sin_f, cos_f, sin_ref, cos_ref;
    Var x;
    Expr t = x / 1000.f;
    const float two_pi = 6.28318530717958647693f;
    sin_f(x) = fast_sin(-two_pi * t + (1 - t) * two_pi);
    cos_f(x) = fast_cos(-two_pi * t + (1 - t) * two_pi);
    sin_ref(x) = sin(-two_pi * t + (1 - t) * two_pi);
    cos_ref(x) = cos(-two_pi * t + (1 - t) * two_pi);
    sin_f.vectorize(x, 8);
    cos_f.vectorize(x, 8);
    sin_ref.vectorize(x, 8);
    cos_ref.vectorize(x, 8);

    double t_fast_sin = 1e6 * benchmark([&]() { sin_f.realize({1000}); });
    double t_fast_cos = 1e6 * benchmark([&]() { cos_f.realize({1000}); });
    double t_sin = 1e6 * benchmark([&]() { sin_ref.realize({1000}); });
    double t_cos = 1e6 * benchmark([&]() { cos_ref.realize({1000}); });

    printf("sin: %f ns per pixel\n"
           "fast_sine: %f ns per pixel\n"
           "cosine: %f ns per pixel\n"
           "fast_cosine: %f ns per pixel\n",
           t_sin, t_fast_sin, t_cos, t_fast_cos);

    if (t_sin < t_fast_sin) {
        printf("fast_sin is not faster than sin\n");
        return 1;
    }

    if (t_cos < t_fast_cos) {
        printf("fast_cos is not faster than cos\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
