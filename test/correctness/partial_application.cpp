#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = 2.0f;

    // implicit for all y
    g(x, _) = f(x, _) + f(x - 1, _);

    // implicit for all x, y on both sides, except for the float which has zero implicit args
    Func h;
    h(_) = (g(_) + f(_)) * 6.0f;

    printf("Realizing function...\n");

    Buffer<float> im = h.realize({4, 4});

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (im(x, y) != 36.0f) {
                printf("im(%d, %d) = %f\n", x, y, im(x, y));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
