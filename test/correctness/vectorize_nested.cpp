#include "Halide.h"
#include "test/common/check_call_graphs.h"

using namespace Halide;

int main(int argc, char **argv) {
    const int width = 16;
    const int height = 16;

    // Type of the test: if only
    // Func f("f");
    // Var x("x"), y("y"), xi("xi"), yi("yi");

    // f(x, y) = x + y;

    // f.compute_root()
    //     .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
    //     .vectorize(xi)
    //     .vectorize(yi);

    // Type of the test: Allocate
    // Func f, g;
    // Var x, y;
    // f(x, y) = x + y;
    // g(x, y) = f(x, y) + f(x + 1, y);

    // // Nested vectorization should cause a warning.
    // Var xi;
    // g.split(x, x, xi, 8).vectorize(xi);
    // f.compute_at(g, xi).vectorize(x);

    // Buffer<int> result = g.realize(width, height);

    // Type of the test: Allocate + for
    Func f, inlined;
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom r(0, 10, "r");
    inlined(x) = x;
    inlined(x) += r;
    f(x, y) = inlined(x) + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize(width, height);

    for (int iy = 0; iy < height; iy++) {
        for (int ix = 0; ix < width; ix++) {
            printf("%2d ", result(ix, iy));
        }
        printf("\n");
    }

    auto cmp_func = [](int x, int y) {
        return x + y + 45;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }
    // Type of the test: For only
    // Func f;
    // Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");
    // f(x, y, c) = x + y + c;

    // f.compute_root()
    //     .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
    //     .reorder(c, xi, yi, x, y)
    //     .vectorize(xi)
    //     .vectorize(yi);

    // Buffer<int> result = f.realize(width, height, 3);

    // for (int ic = 0; ic < 3; ic++) {
    //     for (int iy = 0; iy < height; iy++) {
    //         for (int ix = 0; ix < width; ix++) {
    //             printf("%2d ", result(ix, iy, ic));
    //         }
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // auto cmp_func = [](int x, int y, int c) {
    //     return x + y + c;
    // };
    // if (check_image(result, cmp_func)) {
    //     return -1;
    // }

    printf("Success\n");
    return 0;
}
