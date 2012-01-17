#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Var xo, xi, yo, yi;
    
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = 2.0f;
    g(x, y) = f(x+1, y) + f(x-1, y);

    g.tile(x, y, xo, yo, xi, yi, 8, 8);
    f.chunk(xo);
    
    if (use_gpu()) {
        g.cuda(xo, yo, xi, yi);
        // Tell f to directly use the thread ids for x and y
        f.rename(x, Var("threadidx")).rename(y, Var("threadidy"));
    }

    printf("Realizing function...\n");

    Image<float> im = g.realize(32, 32);

    for (size_t i = 0; i < 32; i++) {
        for (size_t j = 0; j < 32; j++) {
            if (im(i,j) != 4.0) {
                printf("im[%d, %d] = %f\n", (int)i, (int)j, im(i,j));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
