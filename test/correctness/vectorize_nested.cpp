#include "Halide.h"
#include "test/common/check_call_graphs.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 4, 2, TailStrategy::RoundUp)
        .vectorize(xi)
        .vectorize(yi);

    f.bound(x, 0, 24).bound(y, 0, 20);

    f.compile_to_c("/Users/vksnk/Work/Halide/test.cc", {});
    Buffer<int> result = f.realize(24, 20);

    for (int iy = 0; iy < 20; iy++) {
        for (int ix = 0; ix < 24; ix++) {
            printf("%2d ", result(ix, iy));  //(result(ix, iy) == 14) ? result(ix, iy) : 99);
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
