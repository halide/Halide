#include "Halide.h"
#include "HalideRuntimeOpenCL.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    if (!t.has_feature(halide_target_feature_opencl)) {
        printf("[SKIP] No OpenCL target enabled.\n");
        return 0;
    }

    const auto *interface = get_device_interface_for_device_api(DeviceAPI::OpenCL);
    assert(interface->compute_capability != nullptr);
    int major, minor;
    int err = interface->compute_capability(nullptr, &major, &minor);
    if (err != 0 || (major == 1 && minor < 2)) {
        printf("[SKIP] OpenCL %d.%d is less than required 1.2.\n", major, minor);
        return 0;
    }

    // Check dynamic allocations into Heap and Texture memory
    for (auto memory_type : {MemoryType::GPUTexture, MemoryType::Heap}) {
        {
            // 1D stores/loads
            Buffer<int> input(100);
            input.fill(10);
            ImageParam param(Int(32), 1, "input");
            param.set(input);
            param.store_in(memory_type);  // check float stores

            Func f("f"), g("g");
            Var x("x"), xi("xi");
            Var y("y");

            f(x) = cast<float>(x);
            g(x) = param(x) + cast<int>(f(2 * x));

            g.gpu_tile(x, xi, 16);

            f.compute_root().store_in(memory_type).gpu_blocks(x);  // store f as integer
            g.output_buffer().store_in(memory_type);

            Buffer<int> out = g.realize({100});
            for (int x = 0; x < 100; x++) {
                int correct = 2 * x + 10;
                if (out(x) != correct) {
                    printf("out[1D][%d](%d) = %d instead of %d\n", (int)memory_type, x, out(x), correct);
                    return 1;
                }
            }
        }
        {
            // 2D stores/loads
            Buffer<int> input(10, 10);
            input.fill(10);
            ImageParam param(Int(32), 2);
            param.set(input);
            param.store_in(memory_type);  // check float stores

            Func f("f"), g("g");
            Var x("x"), xi("xi");
            Var y("y");

            f(x, y) = cast<float>(x + y);
            g(x) = param(x, x) + cast<int>(f(2 * x, x));

            g.gpu_tile(x, xi, 16, TailStrategy::GuardWithIf);

            f.compute_root().store_in(memory_type).gpu_blocks(x, y);  // store f as integer
            g.store_in(memory_type);

            Buffer<int> out = g.realize({10});
            for (int x = 0; x < 10; x++) {
                int correct = 3 * x + 10;
                if (out(x) != correct) {
                    printf("out[2D][%d](%d) = %d instead of %d\n", (int)memory_type, x, out(x), correct);
                    return 1;
                }
            }
        }
        {
            // 3D stores/loads
            Buffer<int> input(10, 10, 10);
            input.fill(10);
            ImageParam param(Int(32), 3);
            param.set(input);
            param.store_in(memory_type);  // check float stores

            Func f("f"), g("g");
            Var x("x"), xi("xi");
            Var y("y"), z("z");

            f(x, y, z) = cast<float>(x + y + z);
            g(x) = param(x, x, x) + cast<int>(f(2 * x, x, x));

            g.gpu_tile(x, xi, 16, TailStrategy::GuardWithIf);

            f.compute_root().store_in(memory_type).gpu_blocks(x, y, z);  // store f as integer

            g.store_in(memory_type);

            Buffer<int> out = g.realize({10});
            for (int x = 0; x < 10; x++) {
                int correct = 4 * x + 10;
                if (out(x) != correct) {
                    printf("out[3D][%d](%d) = %d instead of %d\n", (int)memory_type, x, out(x), correct);
                    return 1;
                }
            }
        }
        {
            // 1D offset
            Buffer<int> input(100);
            input.set_min(5);
            input.fill(10);
            ImageParam param(Int(32), 1);
            param.set(input);
            param.store_in(memory_type);  // check float stores

            Func f("f"), g("g");
            Var x("x"), xi("xi");
            Var y("y");

            f(x) = cast<float>(x);
            g(x) = param(x) + cast<int>(f(2 * x));

            g.gpu_tile(x, xi, 16, TailStrategy::GuardWithIf);

            f.compute_root().store_in(memory_type).gpu_blocks(x);  // store f as integer
            g.store_in(memory_type);

            Buffer<int> out(10);
            out.set_min(10);
            g.realize(out);
            out.copy_to_host();
            for (int x = 10; x < 20; x++) {
                int correct = 2 * x + 10;
                if (out(x) != correct) {
                    printf("out[1D-shift][%d](%d) = %d instead of %d\n", (int)memory_type, x, out(x), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
