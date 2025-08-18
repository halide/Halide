#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(CastHandleTest, Basic) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support Param<> for pointer types.";
    }

    Func f, g;
    Var x;
    Param<void *> handle;

    f(x) = reinterpret<uint64_t>(handle);
    g(x) = reinterpret<uint64_t>(handle);

    int foo;

    handle.set(&foo);

    Buffer<uint64_t> out1 = f.realize({4});

    g.vectorize(x, 4);
    Buffer<uint64_t> out2 = g.realize({4});

    uint64_t correct = (uint64_t)((uintptr_t)(&foo));

    for (int x = 0; x < out1.width(); x++) {
        EXPECT_EQ(out1(x), correct) << "out1(" << x << ") = " << out1(x) << " instead of " << correct;
        EXPECT_EQ(out2(x), correct) << "out2(" << x << ") = " << out2(x) << " instead of " << correct;
    }
}
