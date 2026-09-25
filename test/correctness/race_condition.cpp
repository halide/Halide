#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("race_condition", "may introduce a race condition resulting in incorrect output.", []() {
        Func f, g;
        Var x, y;

        f(x, y) = 0;

        RDom r(0, 10, 0, 10);
        f(r.x, r.y) += f(r.y, r.x);

        // This schedule should be forbidden, because it causes a race condition.
        f.update().parallel(r.y);
    });
}
