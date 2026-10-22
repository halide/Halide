#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_store_at", "Func \"f\" is computed at the following invalid location:\n  f.store_at(h, y).compute_root();", []() {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        f(x) = x;
        g(x) = f(x);
        h(x, y) = g(x);

        g.compute_at(h, y);

        // This makes no sense, because the compute_at level is higher than the store_at level
        f.store_at(h, y).compute_root();

        h.realize({10, 10});
    });
}
