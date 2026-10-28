#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("float_arg", "Implicit cast from float32 to int in argument 1", []() {
        Func f;
        Var x, y;
        f(x, y) = 3 * x + y;

        // Should result in an error
        Func g;
        g(x) = f(f(x, 3) * 17.0f, 3);
    });
}
