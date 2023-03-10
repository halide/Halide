#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    printf("Defining function...\n");

    f(x, y) = max(x, y);
    g(x, y) = min(x, y);
    h(x, y) = clamp(x + y, 20, 100);

    Var xo("xo"), yo("yo"), xi("xi"), yi("yi");

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        // Resolve why OpenCL used 32,1 tiling
        f.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        g.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        h.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 32);
        g.hexagon().vectorize(x, 32);
        h.hexagon().vectorize(x, 32);
    }

    printf("Realizing function...\n");

    Buffer<int> imf = f.realize({32, 32}, target);
    Buffer<int> img = g.realize({32, 32}, target);
    Buffer<int> imh = h.realize({32, 32}, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != (i > j ? i : j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return 1;
            }
            if (img(i, j) != (i < j ? i : j)) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return 1;
            }
            int href = i + j;
            if (href < 20) href = 20;
            if (href > 100) href = 100;
            if (imh(i, j) != href) {
                printf("imh[%d, %d] = %d (not %d)\n", i, j, imh(i, j), href);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
