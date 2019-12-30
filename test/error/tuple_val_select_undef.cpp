#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x");
    Func f("f");

    // Should result in an error
    f(x) = {x, select(x < 20, 20 * x, undef<int>())};
    f.realize(10);

    printf("Success!\n");
    return 0;
}
