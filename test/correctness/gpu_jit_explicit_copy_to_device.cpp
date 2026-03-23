#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    // We'll have two input buffers. For one we'll copy to the device
    // explicitly. For the other we'll do a device malloc and set
    // host_dirty.
    for (DeviceAPI d : {DeviceAPI::Default_GPU, DeviceAPI::CUDA, DeviceAPI::OpenCL}) {
        if (!get_device_interface_for_device_api(d)) continue;

        Buffer<float> a(100, 100), b(100, 100);

        assert(!a.host_dirty());
        a.fill(2.0f);
        assert(!a.has_device_allocation());
        assert(a.host_dirty());
        a.copy_to_device(d);
        assert(a.has_device_allocation());
        assert(!a.host_dirty());

        assert(!b.host_dirty());
        b.fill(3.0f);
        assert(!b.has_device_allocation());
        assert(b.host_dirty());
        b.device_malloc(d);
        assert(b.has_device_allocation());
        assert(b.host_dirty());

        Func f;
        Var x, y, tx, ty;
        f(x, y) = a(x, y) + b(x, y) + 2;
        f.gpu_tile(x, y, tx, ty, 8, 8, TailStrategy::Auto, d);

        Buffer<float> out = f.realize({100, 100});

        out.for_each_value([&](float f) {
            if (f != 7.0f) {
                printf("%f != 4.0f\n", f);
                abort();
            }
        });
    }

    printf("Success!\n");
    return 0;
}
