#include "Halide.h"
#include <stdio.h>

using namespace Halide;

template<typename T>
bool test() {
    Var x;
    Func f;
    f(x) = cast<T>(x) % 2;

    Buffer<T> im = f.realize({16});

    for (int i = 0; i < 16; i++) {
        if (im(i) != (T)(i % 2)) {
            printf("Mod error for %d %% 2 == %f\n", i, (double)(im(i)));
            return false;
        }
    }

    // Test for negative mod case. Modulous of a negative number by a
    // positive one in Halide is always positive and is such that the
    // same pattern repeats endlessly across the number line.
    // Like so:
    // x:     ... -7 -6 -5 -4 -3 -2 -1  0  1  2  3  4  5  6  7 ...
    // x % 4: ...  1  2  3  0  1  2  3  0  1  2  3  4  0  1  2 ...
    Func nf;
    nf(x) = cast<T>(-x) % 4;

    Buffer<T> nim = nf.realize({16});

    for (int i = 1; i < 16; i++) {
        if (nim(i) != (T)((4 - (i % 4)) % 4)) {
            printf("Mod error for %d %% 4 == %f\n", -i, (double)(nim(i)));
            return false;
        }
    }

    return true;
}

int main(int argc, char **argv) {

    if (test<float>() &&
        test<double>() &&
        test<int32_t>() &&
        test<uint32_t>() &&
        test<int16_t>() &&
        test<uint16_t>() &&
        test<int8_t>() &&
        test<uint8_t>()) {
        printf("Success!\n");
        return 0;
    }

    printf("Failure!\n");
    return 1;
}
