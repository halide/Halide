#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("compute_with_crossing_edges1", "Invalid compute_with: impossible to establish correct stage order between f.s0 with g.s0 and f.s2 with g.s0", []() {
        Var x("x"), y("y");
        Func f("f"), g("g");

        f(x, y) = x + y;
        f(x, y) += 1;
        f(x, y) += 1;

        g(x, y) = x - y;

        f.compute_root();
        g.compute_root();

        f.compute_with(g, y);
        f.update(1).compute_with(g, y);

        Pipeline p({f, g});
        p.realize({200, 200});
    });
}
