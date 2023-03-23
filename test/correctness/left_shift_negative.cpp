#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;
    f(x) = cast<int16_t>(-x);
    g(x) = cast<uint16_t>(x % 8);

    f.compute_root();
    g.compute_root();

    // In Halide, a << b should be equivalent to a * (1 << b), even if
    // a is negative. This test exercises a specific case in which
    // this did not hold in the past.
    Func h1;
    h1(x) = f(x) << g(x);

    Func powers;
    powers(x) = cast<int16_t>(1) << g(x);
    powers.compute_root();
    Func h2;
    h2(x) = f(x) * powers(x);

    h1.vectorize(x, 16);
    h2.vectorize(x, 16);

    Buffer<int16_t> im1 = h1.realize({1024});
    Buffer<int16_t> im2 = h2.realize({1024});

    for (int i = 0; i < im1.width(); i++) {
        if (im1(i) != im2(i)) {
            printf("im1(%d) = %d, im2(%d) = %d\n",
                   i, im1(i), i, im2(i));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
