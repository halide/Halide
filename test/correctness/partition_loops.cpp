#include "Halide.h"
#include <stdio.h>
using namespace Halide;

int main(int argc, char *argv[]) {
    Buffer<uint8_t> input(1024, 1024, 3);

    for (int c = 0; c < input.channels(); c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = x + y + c;
            }
        }
    }

    Var x("x"), y("y"), c("c");

    Func clamped_input = Halide::BoundaryConditions::repeat_edge(input);

    // One of the possible conditions for partitioning loop 'f.s0.x' is
    // ((f.s0.x + g[0]) <= 1023) which depends on 'g'. Since 'g' is
    // only allocated inside f.s0.x, partition loops should not use this
    // condition to compute the epilogue/prologue.
    Func f("f"), g("g"), h("h");
    g(x, y, c) = x + y + c;
    g(x, y, 0) = x;
    h(x, y) = clamped_input(x + g(x, y, 0), y, 2);
    f(x, y, c) = select(h(x, y) < x + y, x + y, y + c);

    f.compute_root();

    Func output("output");
    output(x, y, c) = cast<float>(f(x, y, c));
    Buffer<float> im = output.realize({1024, 1024, 3});

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            for (int c = 0; c < input.channels(); c++) {
                float correct = (input(std::min(2 * x, input.width() - 1), y, 2) < x + y) ? x + y : y + c;
                if (im(x, y, c) != correct) {
                    printf("im(%d, %d, %d) = %f instead of %f\n",
                           x, y, c, im(x, y, c), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
