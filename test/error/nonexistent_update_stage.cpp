#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    f.update().vectorize(x, 4);

    return 0;
}
