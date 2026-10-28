#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("rdom_where_races", "as parallel or vectorized may introduce a race condition resulting in incorrect output.", []() {
        Func f;
        Var x;

        RDom r(0, 10);
        f(x) = 1;
        r.where(f(0) == 1);
        f(r) = 2;

        f.update().parallel(r);
    });
}
