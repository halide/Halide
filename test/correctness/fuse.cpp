#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x("x"), y("y");

    Expr e = x*3 + y;
    f(x, y) = e;
    g(x, y) = e;

    f.compute_root();
    Var xi("xi"), xo("xo"), yi("yi"), yo("yo"), fused("fused");

    // Let's try a really complicated schedule that uses split,
    // reorder, and fuse.  Tile g, then fuse the tile indices into a
    // single var, and fuse the within tile indices into a single var,
    // then tile those two vars again, and do the same fusion
    // again. Neither of the tilings divide the region we're going to
    // evaluate. Finally, vectorize across the resulting y dimension,
    // whatever that means.

    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 3, 5).fuse(xo, yo, y).fuse(xi, yi, x)
        .tile(x, y, xo, yo, xi, yi, 7, 6).fuse(xo, yo, y).fuse(xi, yi, x).vectorize(y, 4);

    RDom r(-16, 32, -16, 32);
    Func error;
    error() = maximum(abs(f(r.x, r.y) - g(r.x, r.y)));

    int err = evaluate_may_gpu<uint32_t>(error());
    if (err != 0) {
        printf("Fusion caused a difference in the output\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}



