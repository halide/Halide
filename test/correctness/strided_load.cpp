#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<int8_t> im(1697);

    // A strided load with stride two loads a pair of vectors and
    // shuffles out the elements like so:
    // A0 A1 A2 A3 B0 B1 B2 B3 -> A0 A2 B0 B2

    // That technique applied to the following would read beyond the
    // input, so the second load actually gets pushed backwards (or
    // valgrind would complain).
    Func f, g;
    Var x;
    f(x) = im(2 * x);
    f.compute_root().vectorize(x, 16).bound(x, 0, 849);

    // However, it's safe to apply it to this step, because f is an
    // internal allocation, and halide_malloc adds a safety margin.
    g(x) = f(2 * x);
    g.compute_root().vectorize(x, 16).bound(x, 0, 425);  // 24 * 2 = 48 < 49

    // g.compile_to_assembly("/dev/stdout", std::vector<Argument>(), "g");

    g.realize({425});

    printf("Success!\n");
    return 0;
}
