#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;

    // Splatter data all over the place
    RDom r(-10, 20);
    f(x, y) = 17;
    f(r, y) = f(r - 1, y) + 100;
    g(x, y) = f(x + 5, y + 5);

    f.compute_root();
    Buffer<int> result = g.realize({10, 1});

    // The init step of f should fill in (-11, 5) -- (14, 5) inclusive, to
    // cover both the reads done by the update step and g

    // The update step of f should cover (-10, 5) -- (9, 5) inclusive, to
    // cover the reduction domain and the reads done by g

    // The output (g) should read (5, 5) -- (14, 5) from that.

    for (int i = 0; i < 10; i++) {
        int correct = i < 5 ? (1617 + i * 100) : 17;
        if (result(i, 0) != correct) {
            printf("Value at %d should have been %d but was instead %d\n", i, correct, result(i, 0));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
