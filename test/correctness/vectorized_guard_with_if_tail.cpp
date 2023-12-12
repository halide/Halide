#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    for (int i = 0; i < 2; i++) {
        Func f, g;
        f(x) = x;
        g(x) = f(x) * 2;

        g.vectorize(x, 8, TailStrategy::GuardWithIf);

        f.compute_at(g, x);

        // A varying amount of f is required depending on if we're in the steady
        // state of g or the tail. Nonetheless, the amount required has a constant
        // upper bound of 8. Vectorization, unrolling, and variants of store_in that
        // require constant extent should all be able to handle this.
        if (i == 0) {
            f.vectorize(x);
        } else {
            f.unroll(x);
        }
        f.store_in(MemoryType::Register);

        Buffer<int> buf = g.realize({37});

        for (int i = 0; i < buf.width(); i++) {
            int correct = i * 2;
            if (buf(i) != correct) {
                printf("buf(%d) = %d instead of %d\n",
                       i, buf(i), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
