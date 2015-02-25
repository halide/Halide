#include <stdio.h>
#include "Halide.h"
#include "clock.h"

using namespace Halide;

#define W 1024
#define H 160

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    Expr math = cast<float>(x+y);
    for (int i = 0; i < 50; i++) math = sqrt(cos(sin(math)));
    f(x, y) = math;
    g(x, y) = math;

    f.parallel(y);

    Image<float> imf = f.realize(W, H);

    double t1, t2;

    t1 = current_time();
    f.realize(imf);
    t2 = current_time();
    double parallelTime = t2 - t1;

    printf("Realizing g\n");
    Image<float> img = g.realize(W, H);
    printf("Done realizing g\n");

    t1 = current_time();
    g.realize(img);
    t2 = current_time();
    double serialTime = t2 - t1;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (imf(x, y) != img(x, y)) {
                printf("imf(%d, %d) = %f\n", x, y, imf(x, y));
                printf("img(%d, %d) = %f\n", x, y, img(x, y));
                return -1;
            }
        }
    }

    printf("Times: %f %f\n", serialTime, parallelTime);
    double speedup = serialTime / parallelTime;
    printf("Speedup: %f\n", speedup);

    if (speedup < 1.5) {
        fprintf(stderr, "WARNING: Parallel should be faster\n");
        return 0;
    }

    printf("Success!\n");
    return 0;
}
