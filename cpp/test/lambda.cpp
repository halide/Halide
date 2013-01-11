#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f;
    Var x, y;
    f(x, y) = x*y;

    // g is equivalent to f above - a two argument function that
    // returns the product of the arguments
    Func g = lambda(x, y, x*y);

    // Use lambdas and implicit args in the one line
    Image<int> im = lambda(f - g + lambda(x, y, x+y)).realize(10, 10);

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int correct = x+y;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
};
