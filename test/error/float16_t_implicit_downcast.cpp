#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

int main() {
    Func f;
    Var x, y;

    // This should throw an error because downcasting will loose precision which
    // should only happen if the user explicitly asks for it
    f(x, y) = 0.1f;

    Image<float16_t> simple = f.realize(10, 3);

    printf("Should not be reached!\n");
    return 0;
}
