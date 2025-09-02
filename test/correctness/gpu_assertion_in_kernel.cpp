#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

bool errored = false;

TEST(GPUAssertionInKernel, Basic) {
    // Turn on debugging so that the pipeline completes and error
    // checking is done before realize returns. Otherwise errors are
    // discovered too late to call a custom error handler.
    Target t = get_jit_target_from_environment().with_feature(Target::Debug);
    if (!t.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not enabled";
    }
    if (t.has_feature(Target::OpenCL)) {
        GTEST_SKIP() << "OpenCL does not support assertions";
    }
    if (t.has_feature(Target::Metal)) {
        GTEST_SKIP() << "Metal assertions are broken";
    }

    Func f;
    Var c, x;
    f(c, x) = x + c + 3;
    f.bound(c, 0, 3).unroll(c);

    Func g;
    g(c, x) = f(c, x) * 8;

    Var xi;
    g.gpu_tile(x, xi, 8);
    f.compute_at(g, x).gpu_threads(x);

    g.jit_handlers().custom_error = [](JITUserContext *, const char *) { errored = true; };
    g.jit_handlers().custom_print = [](JITUserContext *, const char *) {};

    // Should succeed
    ASSERT_NO_THROW(g.realize({3, 100}, t));  // Metal throws here, too
    EXPECT_FALSE(errored) << "There was not supposed to be an error";

    // Should trap
    ASSERT_NO_THROW(g.realize({4, 100}, t));
    EXPECT_TRUE(errored) << "There was supposed to be an error";
}
