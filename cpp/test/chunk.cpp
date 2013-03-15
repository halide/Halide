#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Var xo, xi, yo, yi;
    
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = 2.0f;
    g(x, y) = f(x+1, y) + f(x-1, y);

    
    /*    
          if (use_gpu()) {
          g.cudaTile(x, y, 8, 8);
          f.cudaChunk(Var("blockidx"), x, y);
          } 
    */
    
    g.tile(x, y, xo, yo, xi, yi, 8, 8);
    f.compute_at(g, xo);

    printf("Realizing function...\n");

    Image<float> im = g.realize(32, 32);

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (im(i,j) != 4.0) {
                printf("im[%d, %d] = %f\n", i, j, im(i,j));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
