#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("store_at_without_compute_at", "Func \"g\" is scheduled store_at(), but is inlined. Funcs that use store_at must also call compute_at.", []() {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        f(x) = x;
        g(x) = f(x);
        h(x, y) = g(x);

        g.store_at(h, y);

        h.realize({10, 10});
    });
}
