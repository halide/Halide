#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    for (int i = 0; i < 20; i++) {
        Var x, y, z;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x - 1, y) + g(x + 1, y);

        g.compute_at(f, y);
        f.parallel(y);

        Buffer<int> im = f.realize({8, 8});
        f.realize(im);

        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                if (im(x, y) != (x - 1) * y + (x + 1) * y) {
                    printf("im(%d, %d) = %d\n", x, y, im(x, y));
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
