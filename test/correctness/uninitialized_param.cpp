#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("uninitialized_param", "Parameter scalar_param does not have a valid scalar value.", []() {
        ImageParam image_param(Int(32), 2, "image_param");
        Param<int> scalar_param("scalar_param");

        Var x("x"), y("y");
        Func f("f");

        f(x, y) = image_param(x, y) + scalar_param;

        Buffer<int> b(10, 10);
        image_param.set(b);

        f.realize({10, 10});
    });
}
