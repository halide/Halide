#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("realization_with_too_many_outputs", "Realization requires 2 output(s) but pipeline produces 1 result(s).", []() {
        Func f;
        Var x;
        f(x) = 42;

        // Should result in an error
        Buffer<int> first(10);
        Buffer<int> second(10);

        Realization r({first, second});
        f.realize(r);
    });
}
