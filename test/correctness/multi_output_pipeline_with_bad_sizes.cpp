#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// TODO: move to error tests

namespace {
bool error_occurred;
void halide_error(JITUserContext *user_context, const char *msg) {
    error_occurred = true;
}
}

TEST(MultiOutputPipelineWithBadSizesTest, Basic) {
    Func f;
    Var x;
    f(x) = Tuple(x, sin(x));

    // These should be the same size
    Buffer<int> x_out(100);
    Buffer<float> sin_x_out(101);

    f.jit_handlers().custom_error = &halide_error;
    error_occurred = false;

    Realization r({x_out, sin_x_out});
    f.realize(r);

    EXPECT_TRUE(error_occurred) << "There should have been an error";
}
