#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Func blur(Func in) {
    Func blurx, blury;
    Var x, y;
    blurx(x, y) = in(x - 1, y) + in(x, y) + in(x + 1, y);
    blury(x, y) = (blurx(x, y - 1) + blurx(x, y) + blurx(x, y + 1)) / 9;

    // Compute blurx at the same level as blury is computed at,
    // wherever that may be. Note that this also means blurx would be
    // included in any specializations of blury.
    blurx.compute_at(blury, Var::outermost());

    return blury;
}

int main(int argc, char **argv) {
    Func fn1, fn2;
    Var x, y;

    fn1(x, y) = x + y;
    fn2(x, y) = 2 * x + 3 * y;

    Func blur_fn1 = blur(fn1);
    Func blur_fn2 = blur(fn2);

    Func out;
    out(x, y) = blur_fn1(x, y) + blur_fn2(x, y);

    Var xi, yi, t;
    out.tile(x, y, xi, yi, 16, 16).fuse(x, y, t).parallel(t);
    blur_fn1.compute_at(out, t);
    blur_fn2.compute_at(out, t);

    Buffer<int> result = out.realize({256, 256});
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            int correct = 3 * x + 4 * y;
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %d instead of %d\n",
                       x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
