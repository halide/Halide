#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = x;

    Var xo, xi;
    f.split(x, xo, xi, 4).split(xo, xo, xi, 4);

    return 0;
}
