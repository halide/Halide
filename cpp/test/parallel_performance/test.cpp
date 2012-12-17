#include "Halide.h"
#include <sys/time.h>

using namespace Halide;

#define W 10240
#define H 16

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    Expr math = cast<float>(x+y);
    for (int i = 0; i < 10; i++) math = sqrt(cos(sin(math)));
    f(x, y) = math;
    g(x, y) = math;

    f.parallel(y);

    Image<float> imf = f.realize(W, H);

    timeval t1, t2;

    gettimeofday(&t1, NULL);
    f.realize(W, H);
    gettimeofday(&t2, NULL);

    double parallelTime = (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_usec - t1.tv_usec)/1000.0;

    Image<float> img = g.realize(W, H);

    gettimeofday(&t1, NULL);
    g.realize(W, H);
    gettimeofday(&t2, NULL);
    double serialTime = (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_usec - t1.tv_usec)/1000.0;

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
    
    printf("Success!");
    return 0;
}
