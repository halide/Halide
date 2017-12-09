#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    f(x, y) = x + y;
    g(x, y) = x - y;
    h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

    f.compute_root();
    g.compute_root();

    Param<bool> tile;
    Var xo("xo"), xi("xi");
    g.specialize(tile).split(x, xo, xi, 8);
    g.compute_with(f.specialize(tile), y, LoopAlignStrategy::AlignEnd);

    tile.set(true);
    h.realize(200, 200);

    return 0;
}