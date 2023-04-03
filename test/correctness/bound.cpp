#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, c;
    Func f, g, h;

    f(x, y) = max(x, y);
    g(x, y, c) = f(x, y) * c;

    g.bound(c, 0, 3);

    Buffer<int> imf = f.realize({32, 32});
    Buffer<int> img = g.realize({32, 32, 3});

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != (i > j ? i : j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return 1;
            }
            for (int c = 0; c < 3; c++) {
                if (img(i, j, c) != c * (i > j ? i : j)) {
                    printf("img[%d, %d, %d] = %d\n", i, j, c, img(i, j, c));
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
