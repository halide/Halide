#include <stdio.h>
#include <type_traits>

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;
    f(x, y) = 3 * x + y;

    // Should result in an error
    Func g;
    g(x) = f(f(x, 3) * 17.0f, 3);

    printf("Success!\n");
    return 0;
}
