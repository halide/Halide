#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Test situations where the args to realize specify a size that's
    // too small to realize into, due to scattering or scheduling.

    {
        Func im;
        Var x, y;
        im(x, y) = x + y;

        Func hist;
        RDom r(0, 100, 0, 100);
        hist(im(r.x, r.y)) += 1;

        // Hist can only be realized over 256 values, so if we ask for
        // less we get a cropped view.
        Buffer<int> h = hist.realize({100});
        for (int i = 0; i < 100; i++) {
            // There's one zero at the top left corner, two ones, three twos, etc.
            int correct = i + 1;
            if (h(i) != correct) {
                printf("hist(%d) = %d instead of %d\n", i, h(i), correct);
                return 1;
            }
        }
    }

    {
        Func f;
        Var x, y;
        f(x, y) = x + y;

        Var xi, yi;
        f.tile(x, y, xi, yi, 16, 8, TailStrategy::RoundUp);

        Buffer<int> buf = f.realize({30, 20});

        // There's no way to realize over that domain with the given
        // schedule. Instead Halide has realized a 32x24 buffer and
        // returned a crop of it.
        if (buf.dim(0).extent() != 30 || buf.dim(1).extent() != 20) {
            printf("Incorrect size: %d %d\n", buf.dim(0).extent(), buf.dim(1).extent());
            return 1;
        }

        if (buf.dim(0).stride() != 1 || buf.dim(1).stride() != 32) {
            printf("Incorrect stride: %d %d\n", buf.dim(0).stride(), buf.dim(1).stride());
            return 1;
        }

        for (int y = 0; y < 20; y++) {
            for (int x = 0; x < 30; x++) {
                int correct = x + y;
                if (buf(x, y) != correct) {
                    printf("buf(%d, %d) = %d instead of %d\n", x, y, buf(x, y), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
