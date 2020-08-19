#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

#define W 1024
#define H 160

Var x, y;

double compare_parallel(const char *name, Expr math, bool par_x, bool par_y) {
    Func f, g;
    f(x, y) = math;
    g(x, y) = math;

    if (par_x) {
        f.parallel(x);
    }
    if (par_y) {
        f.parallel(y);
    }

    Buffer<float> imf = f.realize(W, H);

    double parallelTime = benchmark([&]() { f.realize(imf); });

    Buffer<float> img = g.realize(W, H);

    double serialTime = benchmark([&]() { g.realize(img); });

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (imf(x, y) != img(x, y)) {
              printf("%s: imf(%d, %d) = %f\n", name, x, y, imf(x, y));
              printf("%s: img(%d, %d) = %f\n", name, x, y, img(x, y));
                return -1.0;
            }
        }
    }

    printf("%s Times: %f %f\n", name, serialTime, parallelTime);
    double speedup = serialTime / parallelTime;
    printf("%s Speedup: %f\n", name, speedup);

    return speedup;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Expr cheap = cast<float>(x + y);
    compare_parallel("Cheap Inner", cheap, true, false);
    compare_parallel("Cheap Nested", cheap, true, true);

    Expr math = cheap;
    for (int i = 0; i < 50; i++) {
        math = sqrt(cos(sin(math)));
    }

    double speedup = compare_parallel("Expensive", math, false, true);

    if (speedup < 1.5) {
        fprintf(stderr, "WARNING: Parallel should be faster\n");
        return 0;
    }

    printf("Success!\n");
    return 0;
}
