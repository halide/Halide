#include "Halide.h"

#include <cstdio>

using namespace Halide;

// Releases the device before it was ever used, and twice in a row after use.

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    const halide_device_interface_t *device = get_device_interface_for_device_api(DeviceAPI::Default_GPU, target);
    if (!device) {
        printf("[SKIP] No device interface for the default GPU API.\n");
        return 0;
    }

    device->device_release(nullptr, device);

    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    f(x) = x * 2;
    f.gpu_tile(x, xo, xi, 64);
    Buffer<int> out = f.realize({1024});
    for (int i = 0; i < out.width(); i++) {
        if (out(i) != i * 2) {
            printf("out(%d) = %d instead of %d\n", i, out(i), i * 2);
            return 1;
        }
    }
    out.device_free();

    device->device_release(nullptr, device);
    device->device_release(nullptr, device);

    printf("Success!\n");
    return 0;
}
