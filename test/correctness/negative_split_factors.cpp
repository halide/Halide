#include "Halide.h"
#include "halide_test_dirs.h"
#include <gtest/gtest.h>

using namespace Halide;

// TODO: convert to error test

namespace {
bool error_occurred = false;
void my_error_handler(JITUserContext *user_context, const char *msg) {
    error_occurred = true;
}
}

TEST(NegativeSplitFactorsTest, Basic) {
    // Trying to realize a Pipeline with a negative or zero split factor should
    // error out cleanly, and not for example segfault because the output bounds
    // query returned a garbage buffer.

    Param<int> split;

    Func f;
    Var x;

    f(x) = x;
    f.parallel(x, split);

    split.set(-17);

    f.jit_handlers().custom_error = my_error_handler;

    error_occurred = false;
    f.realize({32});

    EXPECT_TRUE(error_occurred) << "There was supposed to be an error!";
}
