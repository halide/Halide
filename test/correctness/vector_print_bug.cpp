#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = print(x);
    f.vectorize(x, 4);
    f.realize(8);
    return 0;
}
