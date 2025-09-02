#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUArgTypes, Basic) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled";
    }

    Func f, g;
    Var x, y, tx, ty;
    Param<int16_t> foo;

    Expr e = select(foo > x, cast<int16_t>(255), foo + cast<int16_t>(x));
    f(x) = e;
    g(x) = e;

    foo.set(-1);
    f.gpu_tile(x, tx, 8);

    Buffer<int16_t> out = f.realize({256});
    Buffer<int16_t> out2 = g.realize({256});
    out.copy_to_host();

    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(out(i), out2(i)) << "Incorrect result at " << i;
    }
}
