#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    Func f;

    Param<int> k;
    k.set(3);

    f(x) = x * k;

    f.parallel(x);

    Buffer<int> im = f.realize({16});

    for (int i = 0; i < 16; i++) {
        if (im(i) != i * 3) {
            printf("im(%d) = %d\n", i, im(i));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
