#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    f(x, y) = 0;
    g(x, y) = f(x, y);
    Pipeline p(g);

    // This can't possibly be a precondition
    p.add_requirement(x == 4 && f(3, 2) == 5);

    p.realize(100, 100);

    return 0;
}
