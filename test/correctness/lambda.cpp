#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f;
    Var x, y, z;
    f(x, y) = x * y;

    // g is equivalent to f above - a two argument function that
    // returns the product of the arguments
    Func g = lambda(x, y, x * y);

    // Use lambdas and implicit args in the one line
    Buffer<int32_t> im = lambda(f(_) - g(_) + lambda(x, y, x + y)(_)).realize({10, 10});

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int correct = x + y;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                return 1;
            }
        }
    }

    // Test implicits in lambda

    Func h;
    h(x, y, z) = x + y * y + z * z * z;  // Ordering of arguments affects results

    Buffer<int32_t> im2 = lambda(_, z, h(_, z)).realize({10, 10, 10});

    for (int z = 0; z < 10; z++) {
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                int correct = x + y * y + z * z * z;
                if (im2(x, y, z) != correct) {
                    printf("im2(%d, %d, %d) = %d instead of %d\n", x, y, z, im2(x, y, z), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
};
