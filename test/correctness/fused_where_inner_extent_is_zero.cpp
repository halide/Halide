#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;
    f(x, y) = x + y;

    Param<int> inner_extent, outer_extent;
    RDom r(0, inner_extent, 0, outer_extent);

    Func g;
    g(x, y) = 0;
    g(r.x, r.y) = f(r.x, r.y);

    Var fused;
    f.compute_root().fuse(x, y, fused);

    // This used to crash with a divide by zero in the fuse logic.
    for (int i = 0; i < 2; i++) {
        for (int o = 0; o < 2; o++) {
            inner_extent.set(i);
            outer_extent.set(o);
            g.realize(10, 10);
        }
    }

    g.realize(10, 10);

    printf("Success!\n");
    return 0;
}
