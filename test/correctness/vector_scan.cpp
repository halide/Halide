#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;
    RDom r(1, 128);

    f(x) = x;
    f.compute_root();

    g(x) = 0;
    g(r) = g(r - 1) + f(r);

    g.update().atomic(true).vectorize(r, 16);

    Buffer<int> out = g.realize({129});

    for (int i = 0; i < out.width(); i++) {
        int correct = (i * (i + 1)) / 2;
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct);
            return -1;
        }
    }

    printf("Success!\n");

    return 0;
}
