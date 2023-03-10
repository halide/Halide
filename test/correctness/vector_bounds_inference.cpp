#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f("f"), g("g"), h("h");
    Var x, y;

    h(x) = x;
    g(x) = h(x - 1) + h(x + 1);
    f(x, y) = (g(x - 1) + g(x + 1)) + y;

    h.compute_root().vectorize(x, 4);
    g.compute_root().vectorize(x, 4);

    Buffer<int> out = f.realize({36, 2});

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 36; x++) {
            if (out(x, y) != x * 4 + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x * 4 + y);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
