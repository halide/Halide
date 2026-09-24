#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    return error_test("tuple_val_select_undef", "Conditionally-undef values in a Tuple should have the same conditions", []() {
        Var x("x");
        Func f("f");

        // Should result in an error
        f(x) = {x, select(x < 20, 20 * x, undef<int>())};
        f.realize({10});
    });
}
