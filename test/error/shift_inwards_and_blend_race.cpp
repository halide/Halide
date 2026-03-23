#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f;
    Var x;

    f(x) = 0;
    f(x) += 4;

    // This schedule should be forbidden, because it causes a race condition.
    f.update().vectorize(x, 8, TailStrategy::ShiftInwardsAndBlend).parallel(x);

    printf("Success!\n");
    return 0;
}
