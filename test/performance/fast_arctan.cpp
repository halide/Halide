#include "Halide.h"
#include "halide_benchmark.h"

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Func atan_f, atan2_f, atan_ref, atan2_ref;
    Var x, y;
    float range = -10.0f;
    Expr t0 = x / 1000.f;
    Expr t1 = y / 1000.f;
    atan_f(x) = fast_atan(-range * t0 + (1 - t0) * range);
    atan2_f(x, y) = fast_atan2(-range * t0 + (1 - t0) * range,
                               -range * t1 + (1 - t1) * range);
    atan_ref(x) = atan(-range * t0 + (1 - t0) * range);
    atan2_ref(x, y) = atan2(-range * t0 + (1 - t0) * range, -range * t1 + (1 - t1) * range);
    atan_f.vectorize(x, 8);
    atan2_f.vectorize(x, 8);
    atan_ref.vectorize(x, 8);
    atan2_ref.vectorize(x, 8);

    double t_fast_atan = 1e6 * benchmark([&]() { atan_f.realize({1000}); });
    double t_fast_atan2 = 1e3 * benchmark([&]() { atan2_f.realize({1000, 1000}); });
    double t_atan = 1e6 * benchmark([&]() { atan_ref.realize({1000}); });
    double t_atan2 = 1e3 * benchmark([&]() { atan2_ref.realize({1000, 1000}); });

    printf("atan: %f ns per pixel\n"
           "fast_atan: %f ns per pixel\n"
           "atan2: %f ns per pixel\n"
           "fast_atan2: %f ns per pixel\n",
           t_atan, t_fast_atan, t_atan2, t_fast_atan2);

    if (t_atan < t_fast_atan) {
        printf("fast_atan is not faster than atan\n");
        return 1;
    }
    if (t_atan2 < t_fast_atan2) {
        printf("fast_atan2 is not faster than atan\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
