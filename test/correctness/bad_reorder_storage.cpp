#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_reorder_storage", "call to reorder_storage references x twice", []() {
        Var x, y, xi;

        Func f;

        f(x, y) = x;

        f.reorder_storage(x, y, x);
    });
}
