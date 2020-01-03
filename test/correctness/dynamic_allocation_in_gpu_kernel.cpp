#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    Param<int> p;

    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x + p, y + p);

    Var xi, yi;
    g.gpu_tile(x, y, xi, yi, 32, 32);
    f.compute_at(g, xi);

    for (int i = 0; i < 10; i++) {
        p.set(i);
        g.realize(128, 128);
    }

    return 0;
}
