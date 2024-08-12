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

    Func atan_f{"fast_atan"}, atan2_f{"fast_atan2"}, atan_ref{"atan_ref"}, atan2_ref{"atan2_ref"};
    Var x, y;
    const int test_w = 512;
    const int test_h = 256;

    Expr t0 = x / float(test_w);
    Expr t1 = y / float(test_h);
    // To make sure we time mostely the computation of the arctan, and not memory bandwidth,
    // we will compute many arctans per output and sum them. In my testing, GPUs suffer more
    // from bandwith with this test, so we give it more arctangenses to compute per output.
    const int test_d = target.has_gpu_feature() ? 1024 : 64;
    RDom rdom{0, test_d};
    Expr off = rdom / float(test_d) - 0.5f;

    float range = -10.0f;
    atan_f(x, y) = sum(fast_atan(-range * t0 + (1 - t0) * range + off));
    atan2_f(x, y) = sum(fast_atan2(-range * t0 + (1 - t0) * range + off,
                                   -range * t1 + (1 - t1) * range));
    atan_ref(x, y) = sum(atan(-range * t0 + (1 - t0) * range + off));
    atan2_ref(x, y) = sum(atan2(-range * t0 + (1 - t0) * range + off, -range * t1 + (1 - t1) * range));

    if (target.has_gpu_feature()) {
        Var xo, xi;
        Var yo, yi;
        atan_f.never_partition_all();
        atan2_f.never_partition_all();
        atan_ref.never_partition_all();
        atan2_ref.never_partition_all();

        atan_f.gpu_tile(x, y, xo, yo, xi, yi, 32, 16, TailStrategy::ShiftInwards);
        atan_ref.gpu_tile(x, y, xo, yo, xi, yi, 32, 16, TailStrategy::ShiftInwards);

        atan2_f.gpu_tile(x, y, xo, yo, xi, yi, 32, 16, TailStrategy::ShiftInwards);
        atan2_ref.gpu_tile(x, y, xo, yo, xi, yi, 32, 16, TailStrategy::ShiftInwards);
    } else {
        atan_f.vectorize(x, 8);
        atan2_f.vectorize(x, 8);
        atan_ref.vectorize(x, 8);
        atan2_ref.vectorize(x, 8);
    }

    double scale = 1e9 / (double(test_w) * (test_h * test_d));
    // clang-format off
    double t_fast_atan  = scale * benchmark([&]() {    atan_f.realize({test_w, test_h}); });
    double t_fast_atan2 = scale * benchmark([&]() {   atan2_f.realize({test_w, test_h}); });
    double t_atan       = scale * benchmark([&]() {  atan_ref.realize({test_w, test_h}); });
    double t_atan2      = scale * benchmark([&]() { atan2_ref.realize({test_w, test_h}); });
    // clang-format on

    printf("atan: %f ns per pixel\n"
           "fast_atan: %f ns per pixel\n"
           "atan2: %f ns per pixel\n"
           "fast_atan2: %f ns per pixel\n",
           t_atan, t_fast_atan, t_atan2, t_fast_atan2);
    if (target.has_gpu_feature()) {
        if (t_atan * 1.10 < t_fast_atan) {
            printf("fast_atan more than 10%% slower than atan on GPU.\n");
            return 1;
        }
        if (t_atan2 * 1.10 < t_fast_atan2) {
            printf("fast_atan2 more than 10%% slower than atan2 on GPU.\n");
            return 1;
        }
    } else {
        if (t_atan < t_fast_atan) {
            printf("fast_atan is not faster than atan\n");
            return 1;
        }
        if (t_atan2 < t_fast_atan2) {
            printf("fast_atan2 is not faster than atan2\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
