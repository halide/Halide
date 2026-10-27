#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("nonexistent_update_stage", "Call to update with index larger than last defined update stage for Func \"", []() {
        Func f;
        Var x;
        f(x) = x;
        f.update().vectorize(x, 4);
    });
}
