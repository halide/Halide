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

    Target target = get_target_from_environment();
    if (target.has_gpu()) {
        Var xi, yi;
        g.gpu_tile(x, y, 8, 8, GPU_DEFAULT);
        f.compute_at(g, Var("blockidx")).gpu_threads(x, y, GPU_DEFAULT);
    }

    printf("Realizing function...\n");

    Image<float> im = g.realize(32, 32, target);

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
