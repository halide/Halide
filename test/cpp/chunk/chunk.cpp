#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
  Var x, y;
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = 2.0;
    g(x, y) = f(x+1, y) + f(x-1, y);

    Var xo, xi, yo, yi;

    g.split(x, xo, xi, 4);
    g.split(y, yo, yi, 4);
    g.transpose(xo, yi);
    f.chunk(xo, Range(xo*4-1, 6) * Range(yo*4, 4));

    printf("Realizing function...\n");

    assert(f.returnType() == Float(64));
    assert(g.returnType() == Float(64));

    g.trace();

    Image<double> im = g.realize(32, 32);

    for (size_t i = 0; i < 32; i++) {
        if (im(i) != 4.0) {
            for (size_t j = 0; j < 32; j++) {
                printf("im[%d] = %f\n", j, im(j));
            }
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
