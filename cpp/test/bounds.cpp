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

    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        // f.cudaTile(x, y, 16, 16);
        // g.cudaTile(x, y, 16, 16);
        // h.cudaTile(x, y, 16, 16);
        Var bx("blockidx"), by("blockidy"), tx("threadidx"), ty("threadidy");
        h.tile(x, y, bx, by, tx, ty, 16, 16);
    }
 
    printf("Realizing function...\n");

    Image<int> imf = f.realize(32, 32);
    Image<int> img = g.realize(32, 32);
    Image<int> imh = h.realize(32, 32);

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
