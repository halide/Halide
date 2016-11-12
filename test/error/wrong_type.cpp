#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    Buffer<float> im = f.realize(100);

    return 0;
}
