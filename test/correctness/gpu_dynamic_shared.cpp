#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUDynamicShared, Basic) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    if (t.has_feature(Target::Vulkan)) {
        const auto *interface = get_device_interface_for_device_api(DeviceAPI::Vulkan);
        ASSERT_NE(interface, nullptr);
        ASSERT_NE(interface->compute_capability, nullptr);
        int major = 0, minor = 0;
        int err = interface->compute_capability(nullptr, &major, &minor);
        if (err != 0 || (major == 1 && minor < 3)) {
            GTEST_SKIP() << "Vulkan " << major << "." << minor << " is less than required 1.3.";
        }
        if ((t.os == Target::IOS) || (t.os == Target::OSX)) {
            GTEST_SKIP() << "Skipping test for Vulkan on iOS/OSX (MoltenVK doesn't support dynamic LocalSizeId yet)!";
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
            for (int ix = 0; ix < 100; ix++) {
                int correct = 3 * ix;
                ASSERT_EQ(out(ix), correct) << "out[" << per_thread << "|" << (int)memory_type << "](" << ix << ")";
            }
        }
    }
}
