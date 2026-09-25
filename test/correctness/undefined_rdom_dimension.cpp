#include "Halide.h"
#include "expect_user_error.h"
#include <assert.h>
#include <stdio.h>

using namespace Halide;

int error_occurred = false;
void my_error(JITUserContext *ctx, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    return error_test("undefined_rdom_dimension", "Use of undefined RDom dimension: r$y", []() {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y"), c("c");

        RDom r(1, 99, "r");
        g(x, y, c) = 42;
        h(x, y, c) = 88;
        f(x, y, c) = g(x, y, c);
        f(r.x, r.y, c) = f(r.x - 1, r.y, c) + h(r.x, r.y, c);

        Buffer<int32_t> result = f.realize({100, 5, 3});
        (void)result;
    });
}
