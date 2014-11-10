#include <stdio.h>
#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    printf("Defining function...\n");

    f(x, y) = max(x, y);
    g(x, y) = min(x, y);
    h(x, y) = clamp(x+y, 20, 100);

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        // Resolve why OpenCL used 32,1 tiling
        f.gpu_tile(x, y, 8, 8, Device_Default_GPU);
        g.gpu_tile(x, y, 8, 8, Device_Default_GPU);
        h.gpu_tile(x, y, 8, 8, Device_Default_GPU);
    }

    printf("Realizing function...\n");

    Image<int> imf = f.realize(32, 32, target);
    Image<int> img = g.realize(32, 32, target);
    Image<int> imh = h.realize(32, 32, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != (i > j ? i : j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return -1;
            }
            if (img(i, j) != (i < j ? i : j)) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return -1;
            }
            int href = i+j;
            if (href < 20) href = 20;
            if (href > 100) href = 100;
            if (imh(i, j) != href) {
                printf("imh[%d, %d] = %d (not %d)\n", i, j, imh(i, j), href);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
