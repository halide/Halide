#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    if (t.has_feature(Target::Vulkan)) {
        const auto *interface = get_device_interface_for_device_api(DeviceAPI::Vulkan);
        assert(interface->compute_capability != nullptr);
        int major, minor;
        int err = interface->compute_capability(nullptr, &major, &minor);
        if (err != 0 || (major == 1 && minor < 3)) {
            printf("[SKIP] Vulkan %d.%d is less than required 1.3.\n", major, minor);
            return 0;
        }
        if ((t.os == Target::IOS) || (t.os == Target::OSX)) {
            printf("[SKIP] Skipping test for Vulkan on iOS/OSX (MoltenVK doesn't support dynamic LocalSizeId yet)!\n");
            return 0;
        }
    }

    // Check dynamic allocations per-block and per-thread into both
    // shared and global
    for (int per_thread = 0; per_thread < 2; per_thread++) {
        for (auto memory_type : {MemoryType::GPUShared, MemoryType::Heap}) {
            Func f("f"), g("g");
            Var x("x"), xi("xi");

            f(x) = x;
            g(x) = f(x) + f(2 * x);

            g.gpu_tile(x, xi, 16);
            if (per_thread) {
                f.compute_at(g, xi);
            } else {
                f.compute_at(g, x).gpu_threads(x);
            }

            f.store_in(memory_type);

            // The amount of shared/heap memory required varies with x
            Buffer<int> out = g.realize({100});
            for (int x = 0; x < 100; x++) {
                int correct = 3 * x;
                if (out(x) != correct) {
                    printf("out[%d|%d](%d) = %d instead of %d\n",
                           per_thread, (int)memory_type, x, out(x), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
