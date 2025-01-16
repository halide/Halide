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
    std::vector<int> indices0;
    std::vector<int> indices1;
    if (!target.has_gpu_feature() || target.has_feature(Target::Feature::OpenCL) || target.has_feature(Target::Feature::CUDA)) {
        indices0 = {3, 1, 6, 7, 2, 4, 0, 5};
        indices1 = {1, 0, 3, 4, 7, 0, 5, 2};
    } else {
        indices0 = {3, 1, 6, 7};
        indices1 = {1, 0, 3, 4};
    }
    Expr shuffle1 = Internal::Shuffle::make({vec1, vec2}, indices0);
    Expr shuffle2 = Internal::Shuffle::make({vec1, vec2}, indices1);
    Expr result = shuffle1 * shuffle2;

    // Manual logarithmic reduce.
    while (result.type().lanes() > 1) {
        int half_lanes = result.type().lanes() / 2;
        Expr half1 = Halide::Internal::Shuffle::make_slice(result, 0, 1, half_lanes);
        Expr half2 = Halide::Internal::Shuffle::make_slice(result, half_lanes, 1, half_lanes);
        result = half1 + half2;
    }
    g(x) = result;

    f0.compute_root();
    f1.compute_root();
    if (target.has_gpu_feature()) {
        Var xo, xi;
        g.gpu_tile(x, xo, xi, 8).never_partition_all();
    }

    Buffer<int> im = g.realize({32}, target);
    im.copy_to_host();
    for (int x = 0; x < 32; x++) {
        int exp = 0;
        int halfway = int(indices0.size() / 2);
        for (size_t i = 0; i < indices0.size(); ++i) {
            int v0 = x * (indices0[i] + (indices0[i] >= halfway ? 3 : 1));
            int v1 = x * (indices1[i] + (indices1[i] >= halfway ? 3 : 1));
            exp += v0 * v1;
        }
        if (im(x) != exp) {
            printf("im[%d] = %d (expected %d)\n", x, im(x), exp);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
