#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = 0;
    f(x) += 5;

    f.vectorize(x, 8);

    f.realize({1024});

    return 0;
}
