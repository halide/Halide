#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::Feature::Vulkan)) {
        std::printf("[SKIP] Vulkan seems to be not working.\n");
        return 0;
    }

    Var x{"x"}, y{"y"};

    Func f0{"f0"}, f1{"f1"}, g{"g"};
    f0(x, y) = x * (y + 1);
    f1(x, y) = x * (y + 3);
    Expr vec1 = Internal::Shuffle::make_concat({f0(x, 0), f0(x, 1), f0(x, 2), f0(x, 3)});
    Expr vec2 = Internal::Shuffle::make_concat({f1(x, 4), f1(x, 5), f1(x, 6), f1(x, 7)});
    std::vector<int> indices0 = {3, 1, 6, 7, 2, 4, 0, 5};
    std::vector<int> indices1 = {1, 0, 3, 4, 7, 0, 5, 2};
    Expr shuffle1 = Internal::Shuffle::make({vec1, vec2}, indices0);
    Expr shuffle2 = Internal::Shuffle::make({vec1, vec2}, indices1);
    Expr result = shuffle1 * shuffle2;

    // Manual logarithmic reduce.
    Expr a_half1 = Halide::Internal::Shuffle::make_slice(result, 0, 1, 4);
    Expr a_half2 = Halide::Internal::Shuffle::make_slice(result, 4, 1, 4);
    Expr a_sumhalves = a_half1 + a_half2;
    Expr b_half1 = Halide::Internal::Shuffle::make_slice(a_sumhalves, 0, 1, 2);
    Expr b_half2 = Halide::Internal::Shuffle::make_slice(a_sumhalves, 2, 1, 2);
    Expr b_sumhalves = b_half1 + b_half2;
    g(x) = Internal::Shuffle::make_extract_element(b_sumhalves, 0) +
           Internal::Shuffle::make_extract_element(b_sumhalves, 1);

    f0.compute_root();
    f1.compute_root();
    if (target.has_gpu_feature()) {
        Var xo, xi;
        g.gpu_tile(x, xo, xi, 8).never_partition_all();
    }

    Buffer<int> im = g.realize({32}, target);
    im.copy_to_host();
    for (int x = 0; x < 32; x++) {
        int fv0[8], fv1[8];
        for (int i = 0; i < 8; ++i) {
            fv0[i] = x * (indices0[i] + (indices0[i] >= 4 ? 3 : 1));
            fv1[i] = x * (indices1[i] + (indices1[i] >= 4 ? 3 : 1));
        }
        int exp = 0;
        for (int i = 0; i < 8; ++i) {
            exp += fv0[i] * fv1[i];
        }
        if (im(x) != exp) {
            printf("im[%d] = %d (expected %d)\n", x, im(x), exp);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
