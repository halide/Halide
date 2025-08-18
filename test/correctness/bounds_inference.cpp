#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BoundsInferenceTest, Basic) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    h(x) = x;
    g(x) = h(x - 1) + h(x + 1);
    f(x, y) = (g(x - 1) + g(x + 1)) + y;

    h.compute_root();
    g.compute_root();

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        g.gpu_tile(x, xo, xi, 128);
        h.gpu_tile(x, xo, xi, 128);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 32);
        g.hexagon().vectorize(x, 32);
        h.hexagon().vectorize(x, 32);
    }

    Buffer<int> out = f.realize({32, 32}, target);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            ASSERT_EQ(out(x, y), x * 4 + y);
        }
    }
}
