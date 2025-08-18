#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ChunkTest, Basic) {
    Var x, y;

    Func f, g;

    f(x, y) = cast<int>(x);
    g(x, y) = f(x + 1, y) + f(x - 1, y);

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xo, yo, xi, yi;
        g.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        f.compute_at(g, xo).gpu_threads(x, y).store_in(MemoryType::GPUShared);
    }

    Buffer<int> im = g.realize({32, 32}, target);

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            ASSERT_EQ(im(i, j), 2 * i)
                << "im[" << i << ", " << j << "] = " << im(i, j)
                << " (expected " << 2 * i << ")";
        }
    }
}
