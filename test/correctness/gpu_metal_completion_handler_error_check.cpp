#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// TODO: Marked DISABLED_ as it doesn't pass on @alexreinking's MacBook Pro
//   with M3 Max. Need a way to make this test more reliable.
TEST(GPUMetalCompletionHandlerErrorCheck, DISABLED_Basic) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::Metal)) {
        GTEST_SKIP() << "Metal not enabled";
    }

    bool errored = false;

    Func f;
    Var c, x, ci, xi;
    RVar rxi;
    RDom r(0, 1000, -327600, 327600);

    // Create a function that is very costly to execute, resulting in a timeout
    // on the GPU
    f(x, c) = x + 0.1f * c;
    f(r.x, c) += cos(sin(tan(cosh(tanh(sinh(exp(tanh(exp(log(tan(cos(exp(f(r.x, c) / cos(cosh(sinh(sin(f(r.x, c))))) / tanh(tan(tan(f(r.x, c)))))))))) + cast<float>(cast<uint8_t>(f(r.x, c) / cast<uint8_t>(log(f(r.x, c))))))))))));

    f.gpu_tile(x, c, xi, ci, 4, 4);
    f.update(0).gpu_tile(r.x, c, rxi, ci, 4, 4);

    // Metal is surprisingly resilient.  Run this in a loop just to make sure we trigger the error.
    for (int i = 0; i < 10 && !errored; i++) {
        auto out = f.realize({1000, 100}, t);
        errored |= out.device_sync() != halide_error_code_success;
    }

    EXPECT_TRUE(errored) << "There was supposed to be an error";
}
