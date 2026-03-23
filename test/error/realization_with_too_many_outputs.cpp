#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = 42;

    // Should result in an error
    Buffer<int> first(10);
    Buffer<int> second(10);

    Realization r({first, second});
    f.realize(r);

    printf("Success!\n");
    return 0;
}
