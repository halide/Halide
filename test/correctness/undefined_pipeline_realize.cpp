#include "Halide.h"
#include "expect_user_error.h"
#include <assert.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("undefined_pipeline_realize", "Func f is defined with 0 dimensions, but realize() is requesting a realization with 3 dimensions.", []() {
        Func f("f");

        Pipeline p(f);
        Buffer<int32_t> result = p.realize({100, 5, 3});
        (void)result;
    });
}
