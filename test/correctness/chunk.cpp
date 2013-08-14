#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Var xo, xi, yo, yi;
    
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = cast<float>(x);
    g(x, y) = f(x+1, y) + f(x-1, y);

    
    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        Var xi, yi;
        g.cuda_tile(x, y, 8, 8);
        f.compute_at(g, Var("blockidx")).cuda_threads(x, y);
    } else {    
        g.tile(x, y, xo, yo, xi, yi, 8, 8);
        f.compute_at(g, xo);
    }

    printf("Realizing function...\n");

    Image<float> im = g.realize(32, 32);

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (im(i,j) != 2*i) {
                printf("im[%d, %d] = %f\n", i, j, im(i,j));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
