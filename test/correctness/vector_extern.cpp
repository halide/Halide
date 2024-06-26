#include "Halide.h"
#include <math.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    printf("Defining function...\n");

    f(x) = sqrt(cast<float>(x));

    f.vectorize(x, 4);
    Buffer<float> im = f.realize({32});

    for (int i = 0; i < 32; i++) {
        float correct = sqrtf((float)i);
        if (fabs(im(i) - correct) > 0.001) {
            printf("im(%d) = %f instead of %f\n", i, im(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
