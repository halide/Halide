#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Param<float> val;

    Func f, g;
    Var x, y;

    f(x, y) = val + cast<uint8_t>(x);
    g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);

    f.compute_root().memoize(EvictionKey(42));
    f.compute_root().memoize(EvictionKey(1764));

    val.set(23.0f);
    Buffer<float> out = g.realize(128, 128);

    printf("Success!\n");
    return 0;
}
