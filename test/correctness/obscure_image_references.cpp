#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam im1(UInt(8), 1);
    Buffer<uint8_t> im2(10), im3(20);
    Param<int> j;

    assert(im1.dimensions() == 1);
    assert(im2.dimensions() == 1);
    assert(im3.dimensions() == 1);

    Func f;
    Var x;
    f(x) = x + im1.width();
    RDom r(0, cast<int>(clamp(im2(j), 0, 99)));
    f(r) = 37;

    im2(3) = 10;

    j.set(3);
    im1.set(im3);
    Buffer<int> result = f.realize({100});

    for (int i = 0; i < 100; i++) {
        int correct = i < im2(3) ? 37 : (i + 20);
        if (result(i) != correct) {
            printf("result(%d) = %d instead of %d\n", i, result(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
