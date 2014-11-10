// Gridding algoithm, adopted from bilateral_grid demo app.
// There's nothing left from bilateral_grid except ideas and typical Halide idioms.

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("No gpu target enabled. Skipping test.\n");
        return 0;
    }

    Var x;
    Func div, mod;
    div(x) = cast<uint32_t>(x) / 4;
    mod(x) = cast<uint32_t>(x) % 4;

    div.vectorize(x, 4).gpu_tile(x, 16);
    mod.vectorize(x, 4).gpu_tile(x, 16);

    Image<uint32_t> Rdiv = div.realize(64);
    Image<uint32_t> Rmod = mod.realize(64);

    for (uint32_t i = 0; i < 64; i++) {
        assert(Rdiv(i) == i / 4);
        assert(Rmod(i) == i % 4);
    }

    printf("Success!");
    return 0;
}



