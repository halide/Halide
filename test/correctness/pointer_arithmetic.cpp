#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("pointer_arithmetic", "Halide does not support pointer arithmetic", []() {
        Param<const char *> p;
        p.set("Hello, world!\n");

        Func f;
        Var x;
        // Should error out during match_types
        f(x) = p + 2;
    });
}
