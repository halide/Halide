#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // A predicated load with a negative stride can address lanes before the
    // start of the buffer. The predicate masks them off, so no out-of-bounds
    // access actually happens, but the codegen still has to cope with the
    // negative indices.
    Func f, g, h;
    Var x;
    f(x) = cast<uint8_t>(x);
    g(x) = f(-x);
    h(x) = g(x) + g(x - 1);
    f.compute_at(g, x);
    g.compute_at(h, x).vectorize(x, 4, TailStrategy::GuardWithIf);

    Buffer<uint8_t> result = h.realize({15});

    for (int i = 0; i < result.width(); i++) {
        uint8_t correct = (uint8_t)(-i) + (uint8_t)(1 - i);
        if (result(i) != correct) {
            printf("result(%d) = %d instead of %d\n", i, result(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
