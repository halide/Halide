#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Param<float> val;

    Func f, g;
    Var x, y;

    f(x, y) = val + cast<uint8_t>(x);
    g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);

    g.split(y, y, _, 16);
    f.store_root();
    f.compute_at(g, y).memoize();

    val.set(23.0f);
    Buffer<uint8_t> out = g.realize(128, 128);

    for (int32_t i = 0; i < 128; i++) {
        for (int32_t j = 0; j < 128; j++) {
            assert(out(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
        }
    }
}
