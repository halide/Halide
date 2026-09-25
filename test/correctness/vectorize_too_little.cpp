#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("vectorize_too_little", "Can't split v0 by 0. Split factors must be strictly positive", []() {
        Var x, y;

        Buffer<int> input(5, 5);
        Func f;
        f(x, y) = input(x, y) * 2;
        f.vectorize(x, 0);

        // Should result in an error
        Buffer<int> out = f.realize({5, 5});
        (void)out;
    });
}
