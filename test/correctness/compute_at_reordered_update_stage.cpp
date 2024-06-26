#include "Halide.h"

using namespace Halide;

// Tests for regression detailed in https://github.com/halide/Halide/issues/3388

int main(int argc, char *argv[]) {
    Func g;

    {
        Func f;
        Var x, y;

        f(x, y) = x + y;

        g(x, y) = 0;
        g(x, y) += f(x, y);

        g.update().reorder(y, x);
        f.store_at(g, x).compute_at(g, y);
    }

    Halide::Buffer<int> out_orig = g.realize({10, 10});

    // This is here solely to test Halider::Buffer::copy()
    Halide::Buffer<int> out = out_orig.copy();

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            const int actual = out(x, y);
            const int expected = x + y;
            if (actual != expected) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, actual, expected);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
