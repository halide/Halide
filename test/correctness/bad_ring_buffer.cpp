#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_ring_buffer", "Func \"f\" is scheduled with ring_buffer(), but has matching store_at and hoist_storage levels. Add an explicit hoist_storage directive to the schedule to fix the issue.", []() {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        f(x) = x;
        g(x) = f(x);
        h(x, y) = g(x);

        g.compute_at(h, y);

        // ring_buffer() requires an explicit hoist_storage().
        f.compute_root().ring_buffer(2);

        h.realize({10, 10});
    });
}
