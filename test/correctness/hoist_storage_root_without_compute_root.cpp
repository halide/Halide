#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

// A Func scheduled hoist_storage_root() but left inlined is an error:
// hoist_storage_root requires a matching compute_root or compute_at.
int main(int argc, char **argv) {
    return error_test("hoist_storage_root_without_compute_root", "Func \"g\" is scheduled hoist_storage_root(), but is inlined. Funcs that use hoist_storage_root must also call compute_root or compute_at.", []() {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        f(x) = x;
        g(x) = f(x);
        h(x, y) = g(x);

        g.hoist_storage_root();

        h.realize({10, 10});
    });
}
