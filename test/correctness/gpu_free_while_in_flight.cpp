#include "Halide.h"

#include <cstdio>

using namespace Halide;

// Frees device buffers while the kernels writing them may still be running,
// then allocates buffers of other sizes, so that the freed regions are split
// and coalesced while the earlier work is in flight. Run under the Vulkan
// validation layers to catch buffers destroyed while in use.

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Internal::JITSharedRuntime::reuse_device_allocations(true);

    // A kernel slow enough to still be running when its output is freed.
    Var x("x"), xo("xo"), xi("xi");
    RDom r(0, 256);
    Func f("f");
    f(x) = cast<uint32_t>(0);
    f(x) = f(x) * 3 + cast<uint32_t>(x + r);
    f.gpu_tile(x, xo, xi, 64, TailStrategy::GuardWithIf);
    f.update().gpu_tile(x, xo, xi, 64, TailStrategy::GuardWithIf);

    auto expected = [](int x) {
        uint32_t v = 0;
        for (int r = 0; r < 256; r++) {
            v = v * 3 + (uint32_t)(x + r);
        }
        return v;
    };

    const int sizes[] = {1 << 20, 1 << 10, 1 << 16, 1 << 20, 1 << 21, 1 << 8};
    for (int iter = 0; iter < 4; iter++) {
        for (int size : sizes) {
            {
                Buffer<uint32_t> freed(size);
                f.realize(freed);
                freed.device_free();
            }

            Buffer<uint32_t> out(size / 2 + 3);
            f.realize(out);
            out.copy_to_host();
            for (int i = 0; i < out.width(); i++) {
                if (out(i) != expected(i)) {
                    printf("out(%d) = %u instead of %u (size %d)\n", i, out(i), expected(i), size);
                    return 1;
                }
            }
        }
    }

    Internal::JITSharedRuntime::reuse_device_allocations(false);

    printf("Success!\n");
    return 0;
}
