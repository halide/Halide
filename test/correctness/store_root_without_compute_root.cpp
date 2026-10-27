#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

// A Func scheduled store_root() but left inlined is an error: store_root
// requires a matching compute_root or compute_at.
int main(int argc, char **argv) {
    return error_test("store_root_without_compute_root", "Func \"g\" is scheduled store_root(), but is inlined. Funcs that use store_root must also call compute_root or compute_at.", []() {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        f(x) = x;
        g(x) = f(x);
        h(x, y) = g(x);

        g.store_root();

        h.realize({10, 10});
    });
}
