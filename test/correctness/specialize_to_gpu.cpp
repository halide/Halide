#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
#ifdef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    printf("[SKIP] Serialization won't preserve GPU buffers, skipping.\n");
    return 0;
#endif

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    // A sequence of stages which may or may not run on the gpu.
    Func f, g, h;
    ImageParam in(Int(32), 1);
    Var x, xi;

    f(x) = in(x) + in(x + 1);
    g(x) = f(x * 2);
    h(x) = g(x) - 7;

    Param<bool> gpu_f, gpu_g, gpu_h;

    f.compute_root().specialize(gpu_f).gpu_tile(x, x, xi, 16);
    g.compute_root().specialize(gpu_g).gpu_tile(x, x, xi, 16);
    h.compute_root().specialize(gpu_h).gpu_tile(x, x, xi, 16);

    Buffer<int> out(128), reference(128), input(256);

    lambda(x, x * 17 + 43 + x * x).realize(input);
    in.set(input);

    gpu_f.set(false);
    gpu_g.set(false);
    gpu_h.set(false);
    h.realize(reference);

    for (int i = 1; i < 8; i++) {
        gpu_f.set((i & 1) != 0);
        gpu_g.set((i & 2) != 0);
        gpu_h.set((i & 4) != 0);

        h.realize(out);

        RDom r(out);
        uint32_t err = evaluate<uint32_t>(sum(abs(out(r) - reference(r))));
        if (err) {
            printf("Incorrect results for test %d\n", i);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
