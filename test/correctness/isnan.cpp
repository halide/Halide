#include "Halide.h"
#include <stdio.h>

using namespace Halide;

/* Halide uses fast-math by default. is_nan must either be used inside
 * strict_float or as a test on inputs produced outside of
 * Halide. Using it to test results produced by math inside Halide but
 * not using strict_float is unreliable. This test covers both of these cases. */

int check_buffer(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
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
    return 0;
}

int main(int argc, char **argv) {

    int w = 16;
    int h = 16;

    {
        Func f;
        Var x;
        Var y;

        Expr e = sqrt(x - y);
        f(x, y) = strict_float(select(is_nan(e), 0.0f, 1.0f));
        f.vectorize(x, 8);

        Buffer<float> im = f.realize(w, h);
        if (check_buffer(im) != 0) {
            return -1;
        }
    }

    {
        Buffer<float> non_halide_produced(w, h);
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                non_halide_produced(x, y) = sqrt(x - y);
            }
        }    

        ImageParam in(Float(32), 2);
        Func f;
        Var x;
        Var y;

        f(x, y) = select(is_nan(in(x, y)), 0.0f, 1.0f);
        f.vectorize(x, 8);
        
        in.set(non_halide_produced);
        Buffer<float> im = f.realize(w, h);
        if (check_buffer(im) != 0) {
            return -1;
        }
    }
        
    printf ("Success\n");
    return 0;
}
