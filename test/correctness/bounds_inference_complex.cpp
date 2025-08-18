#include "Halide.h"
#include "halide_test_dirs.h"

#include <gtest/gtest.h>

using namespace Halide;

TEST(BoundsInferenceTest, Complex) {
    const int K = 8;

    Func f[K];
    Var x, y;

    std::string seed_str = Internal::get_env_variable("HL_TEST_SEED");
    srand(seed_str.empty() ? 0 : atoi(seed_str.c_str()));

    f[0](x, y) = x + y;
    f[1](x, y) = x * y;
    for (int i = 2; i < K; i++) {
        int j1 = rand() % i;
        int j2 = rand() % i;
        int j3 = rand() % i;
        f[i](x, y) = f[j1](x - 1, y - 1) + f[j2](x + 1, clamp(f[j3](x + 1, y - 1), 0, 7));

        if (rand() & 1) {
            f[i].compute_root();
            f[i].vectorize(x, 4);
        }
    }

    Buffer<int> out = f[K - 1].realize({32, 32});
}
