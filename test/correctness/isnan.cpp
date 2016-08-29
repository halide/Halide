#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    Var y;

    Expr e = sqrt(x - y);
    f(x, y) = select(is_nan(e), 0.0f, 1.0f);
    f.vectorize(x, 8);

    int w = 16;
    int h = 16;
    Buffer<float> im = f.realize(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if ((x - y) < 0) {
                if (im(x, y) != 0.0f) {
                    printf ("undetected Nan for sqrt(%d - %d)\n", x, y);
                    return -1;
                }
            } else {
                if (im(x, y) != 1.0f) {
                    printf ("unexpected Nan for sqrt(%d - %d)\n", x, y);
                    return -1;
                }
            }
        }
    }


    printf ("Success\n");
    return 0;
}
