#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_reorder", "call to reorder references v0 twice.", []() {
        Var x, y, xi;

        Func f;

        f(x, y) = x;

        f
            .split(x, x, xi, 8)
            .reorder(x, y, x);

        // Oops, probably meant "xi" rather than x in the reorder call
    });
}
