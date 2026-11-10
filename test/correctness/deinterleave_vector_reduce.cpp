#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // A vector reduction to a scalar whose input contains ramp % 2. The
    // interleaver must not try to deinterleave the scalar result.
    ImageParam in(UInt(8), 1);
    Func f;
    RDom r(0, 8);
    f() = 0;
    f() += cast<int>(in(r / 2)) * (r % 2 + 1);
    f.update().atomic().vectorize(r);

    Buffer<uint8_t> b(4);
    for (int i = 0; i < 4; i++) {
        b(i) = i + 1;
    }
    in.set(b);
    Buffer<int> out = f.realize();

    int correct = 0;
    for (int i = 0; i < 8; i++) {
        correct += (i / 2 + 1) * (i % 2 + 1);
    }
    if (out() != correct) {
        printf("out() = %d instead of %d\n", out(), correct);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
