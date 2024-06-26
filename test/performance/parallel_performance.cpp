#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

#define W 1024
#define H 160

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Var x, y;
    Func f, g;

    Expr math = cast<float>(x + y);
    for (int i = 0; i < 50; i++) {
        math = sqrt(cos(sin(math)));
    }
    f(x, y) = math;
    g(x, y) = math;

    f.parallel(y);

    Buffer<float> imf = f.realize({W, H});

    double parallelTime = benchmark([&]() { f.realize(imf); });

    printf("Realizing g\n");
    Buffer<float> img = g.realize({W, H});
    printf("Done realizing g\n");

    double serialTime = benchmark([&]() { g.realize(img); });

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (imf(x, y) != img(x, y)) {
                printf("imf(%d, %d) = %f\n", x, y, imf(x, y));
                printf("img(%d, %d) = %f\n", x, y, img(x, y));
                return 1;
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
