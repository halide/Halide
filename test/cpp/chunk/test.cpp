#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Var xo("blockidx"), xi("threadidx"), yo("blockidy"), yi("threadidy");
    
    Func f("f"), g("g");

    printf("Defining function...\n");

    // This is kind of ugly -- trying to force g.f to be over threadid{x,y} when chunked
    // Also: CUDA on older machines is unhappy with doubles, so now use floats
    f(xi, yi) = 2.0f;
    g(x, y) = f(x+1, y) + f(x-1, y);

    g.split(x, xo, xi, 8);
    g.split(y, yo, yi, 8);
    g.transpose(xo, yi);
    f.chunk(xo);
    
    if (use_gpu()) {
        g.parallel(xo).parallel(xi).parallel(yo).parallel(yi);
        f.parallel(xi).parallel(yi);
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
