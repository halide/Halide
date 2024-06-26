#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;
    f(x, y) = select(x == y, 1, 0);

    Buffer<int> im = f.realize({10, 10});

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int correct = (x == y) ? 1 : 0;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
