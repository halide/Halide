#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    {
        // Forwards sum-scan
        Func f, g;
        Var x;
        RDom r(1, 128);

        f(x) = cast<uint8_t>(x);
        f.compute_root();

        g(x) = cast<uint8_t>(0);
        g(r) = g(r - 1) + f(r);

        g.update().atomic(true).vectorize(r, 64);

        Buffer<uint8_t> out = g.realize({129});

        for (int i = 0; i < out.width(); i++) {
            uint8_t correct = (i * (i + 1)) / 2;
            if (out(i) != correct) {
                printf("out(%d) = %d instead of %d\n", i, out(i), correct);
                return -1;
            }
        }

        g.compile_to_assembly("/dev/stdout", {}, Target{"host-no_asserts-no_runtime-no_bounds_query"});
    }

    {
        // Backwards sum-scan
        Func f, g;
        Var x;
        RDom r(1, 128);

        f(x) = cast<uint8_t>(128 - x);
        f.compute_root();

        g(x) = cast<uint8_t>(0);
        g(128 - r) = g(128 - r + 1) + f(128 - r);

        g.update().atomic(true).vectorize(r, 64);

        Buffer<uint8_t> out = g.realize({129});

        for (int i = 0; i < out.width(); i++) {
            uint8_t correct = ((128 - i) * ((128 - i) + 1)) / 2;
            if (out(i) != correct) {
                printf("out(%d) = %d instead of %d\n", i, out(i), correct);
                return -1;
            }
        }

        g.compile_to_assembly("/dev/stdout", {}, Target{"host-no_asserts-no_runtime-no_bounds_query"});
    }

    printf("Success!\n");

    return 0;
}
