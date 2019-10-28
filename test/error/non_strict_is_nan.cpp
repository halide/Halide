#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    // Should be error to call is_nan() without strict_float
    f(x) = is_nan(cast<float>(x));

    f.realize(10);

    // You may find yourself living outside of strict_float()
    // And you may ask yourself:
    printf("How did I get here?\n");
}
