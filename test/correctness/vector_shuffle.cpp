#include "Halide.h"
#include <algorithm>
#include <cstdlib>
#include <stdio.h>

using namespace Halide;

int test_with_indices(const Target &target, const std::vector<int> &indices0, const std::vector<int> &indices1) {
    printf("indices0:");
    for (int i : indices0) {
        printf(" %d", i);
    }
    printf("    indices1:");
    for (int i : indices1) {
        printf(" %d", i);
    }
    printf("\n");

    Var x{"x"}, y{"y"};
    Func f0{"f0"}, f1{"f1"}, g{"g"};
    f0(x, y) = x * (y + 1);
    f1(x, y) = x * (y + 3);
    Expr vec1 = Internal::Shuffle::make_concat({f0(x, 0), f0(x, 1), f0(x, 2), f0(x, 3)});
    Expr vec2 = Internal::Shuffle::make_concat({f1(x, 4), f1(x, 5), f1(x, 6), f1(x, 7)});
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
        for (size_t i = 0; i < indices0.size(); ++i) {
            int v0 = x * (indices0[i] + (indices0[i] >= 4 ? 3 : 1));
            int v1 = x * (indices1[i] + (indices1[i] >= 4 ? 3 : 1));
            exp += v0 * v1;
        }
        if (im(x) != exp) {
            printf("im[%d] = %d (expected %d)\n", x, im(x), exp);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    int max_vec_size = 4;
    if (!target.has_gpu_feature() || target.has_feature(Target::Feature::OpenCL) || target.has_feature(Target::Feature::CUDA)) {
        max_vec_size = 8;
    }

    for (int vec_size = max_vec_size; vec_size > 1; vec_size /= 2) {
        printf("Testing vector size %d...\n", vec_size);
        std::vector<int> indices0, indices1;

        // Test 1: All indices: foreward/backward and combined
        for (int i = 0; i < vec_size; ++i) {
            indices0.push_back(i);                 // forward
            indices1.push_back(vec_size - i - 1);  // backward
        }
        printf("  All indices forward...\n");
        if (test_with_indices(target, indices0, indices0)) {
            return 1;
        }
        printf("  All indices backward...\n");
        if (test_with_indices(target, indices1, indices1)) {
            return 1;
        }
        printf("  All indices mixed forware / backward...\n");
        if (test_with_indices(target, indices0, indices1)) {
            return 1;
        }

        // Test 2: Shuffled indices (4 repetitions)
        for (int r = 0; r < 4; ++r) {
            // Shuffle with Fisher-Yates
            for (int i = vec_size - 1; i >= 1; --i) {
                // indices0
                int idx = std::rand() % (i + 1);
                std::swap(indices0[idx], indices0[i]);
                // indices1
                idx = std::rand() % (i + 1);
                std::swap(indices1[idx], indices1[i]);
            }
            printf("  Randomly shuffled...\n");
            if (test_with_indices(target, indices0, indices1)) {
                return 1;
            }
        }

        // Test 3: Interleaved
        indices0.clear();
        indices1.clear();
        for (int i = 0; i < vec_size / 2; ++i) {
            // interleave (A, B)
            indices0.push_back(i);
            indices0.push_back(i + vec_size / 2);

            // interleave (B, A)
            indices1.push_back(i + vec_size / 2);
            indices1.push_back(i);
        }
        printf("  Interleaved...\n");
        if (test_with_indices(target, indices0, indices1)) {
            return 1;
        }

        // Test 4: Concat (not-really, as the input-vectors are size 4, so only if vec_size == 8, it's a concat)
        indices0.clear();
        indices1.clear();
        for (int i = 0; i < vec_size; ++i) {
            // concat (A, B)
            indices0.push_back(i);

            // concat (B, A)
            indices1.push_back((i + vec_size / 2) % (vec_size / 2));
        }
        printf("  Concat...\n");
        if (test_with_indices(target, indices0, indices1)) {
            return 1;
        }

        if (vec_size == 4) {
            indices0 = {1, 3, 2, 0};
            indices1 = {2, 3, 1, 0};

            printf("  Specific index combination, known to have caused problems...\n");
            if (test_with_indices(target, indices0, indices1)) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
