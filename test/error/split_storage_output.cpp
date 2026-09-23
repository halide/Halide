#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    f.split_storage(x, xo, xi, 4);
    f.realize({16});

    return 0;
}
