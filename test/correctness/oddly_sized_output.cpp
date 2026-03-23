#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<int> input(87, 93);
    input.fill(0);

    Func f;
    Var x, y;
    f(x, y) = input(x, y) * 2;

    Var yi;
    f.vectorize(x, 4).unroll(x, 3).unroll(x, 2);
    f.split(y, y, yi, 16).parallel(y);

    Buffer<int> out = f.realize({87, 93});

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            if (out(x, y) != input(x, y) * 2) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), input(x, y) * 2);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
