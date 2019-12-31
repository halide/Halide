#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    int W = 1024;
    int H = 1024;

    Buffer<uint16_t> input(W, H, 3);

    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Var x("x"), y("y"), z("z"), c("c");
    Func f("f"), g("g");
    f(x, y, z, c) = (input(x, y, c) - input(x, z, c));
    g(x, y, c) = f(x, y, (x + y) % 10, c) + f(x, y + 1, (x * y) % 10, c) + f(x, y + 2, (x - y) % 10, c) + f(x + 1, y, (x) % 10, c) + f(x + 2, y, (y) % 10, c);

    Var x_o("x_o"), x_i("x_i"), y_o("y_o"), y_i("y_i"), c_o("c_o"), c_i("c_i"), x_o_vo("x_o_vo"), x_o_vi("x_o_vi");

    g.compute_root()
        .split(x, x_o, x_i, 1)
        .split(y, y_o, y_i, 1)
        .split(c, c_o, c_i, 1)
        .reorder(x_i, y_i, c_i, x_o, y_o, c_o)
        .split(x_o, x_o_vo, x_o_vi, 16)
        .vectorize(x_o_vi)
        .parallel(c_o)
        .parallel(y_o);

    // There used to be a bug where the outer splits (which are
    // no-ops!), caused the inner split to be roundup instead of
    // shiftinwards, which causes out of bounds errors for the output
    // size below.

    // Just check it doesn't fail a bounds assertion.
    Buffer<uint16_t> out = g.realize(input.width() - 2, input.height() - 2, 3);

    return 0;
}
