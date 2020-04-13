#include "Halide.h"
#include "test/common/check_call_graphs.h"

using namespace Halide;

int main(int argc, char **argv) {
    const int width = 37;
    const int height = 22;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
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
        return x + y;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    printf("Success\n");
    return 0;
}
