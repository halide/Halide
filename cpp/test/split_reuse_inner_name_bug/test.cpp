#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), xo("xo");
    Func f("f");

    f(x) = 1;
    f.root().split(x, xo, x, 2);
    f.realize(30);

    printf("Success!\n");
    return 0;
}
