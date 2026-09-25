#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("vectorize_too_much", "is accessed at -3, which is before the min (0) in dimension 0", []() {
        Var x, y;

        Buffer<int> input(5, 5);
        Func f;
        f(x, y) = input(x, y) * 2;
        f.vectorize(x, 8).vectorize(y, 8);

        // Should result in an error
        Buffer<int> out = f.realize({5, 5});
        (void)out;
    });
}
