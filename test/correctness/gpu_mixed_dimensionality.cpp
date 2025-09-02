#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUMixedDimensionality, Basic) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    Func f("f"), g("g"), h("h"), out("out");
    Var x("x"), y("y"), z("z");

    f(x, y, z) = x + y + z;
    f(x, y, z) += 1;
    g(x, y, z) = f(x, y, z);
    g(x, y, z) += 1;
    h(x, y, z) = g(x, y, z);
    h(x, y, z) += 1;
    out(x, y, z) = h(x, y, z);
    out(x, y, z) += 1;

    Var xi("xi"), yi("yi"), zi("zi");
    out.gpu_tile(x, y, z, xi, yi, zi, 4, 4, 4);
    out.update().gpu_tile(x, y, xi, yi, 4, 4);
    h.compute_at(out, x).gpu_threads(x, y);
    h.update().gpu_threads(x);
    // TODO: NormalizeDimensionality in FuseGPUThreadLoops.cpp doesn't work in the following case.
    // g.compute_at(h, y).gpu_threads(x);
    // g.update();
    g.compute_at(h, x);
    g.update();
    f.compute_at(g, x);
    f.update();

    Buffer<int> o = out.realize({64, 64, 64});

    for (int zz = 0; zz < 64; zz++) {
        for (int yy = 0; yy < 64; yy++) {
            for (int xx = 0; xx < 64; xx++) {
                int correct = xx + yy + zz + 4;
                ASSERT_EQ(o(xx, yy, zz), correct) << "out(" << xx << ", " << yy << ", " << zz << ")";
            }
        }
    }
}
