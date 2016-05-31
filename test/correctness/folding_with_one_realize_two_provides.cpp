#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = cast<float>(x + 10*y);

    Func g("g");
    g(x, y) = f(x, y-1) + f(x, y+1);

    f.compute_at(g, y).store_root().vectorize(x, 8);

    g.specialize(g.output_buffer().width() < 64).vectorize(x, 8);

    g.realize(32, 24);

    return 0;
}
