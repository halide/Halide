#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
#ifdef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    printf("[SKIP] Serialization won't preserve GPU buffers, skipping.\n");
    return 0;
#endif

    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    // Make an uninitialized host buffer
    Buffer<float> in(100, 100);

    // Check it's considered uninitialized
    assert(!in.has_device_allocation());
    assert(!in.host_dirty() && !in.device_dirty());

    // Fill it with a value, and check it's considered initialized on
    // the host.
    in.fill(7.0f);
    assert(!in.has_device_allocation());
    assert(in.host_dirty() && !in.device_dirty());

    // Run a pipeline that uses it as an input
    Func f;
    Var x, y, xi, yi;
    f(x, y) = in(x, y);
    f.gpu_tile(x, y, xi, yi, 8, 8);
    Buffer<float> out = f.realize({100, 100});

    // Check the output has a device allocation, and was copied to
    // host by realize.
    assert(out.has_device_allocation());
    assert(!out.host_dirty() && !out.device_dirty());

    // Check the input how has a device allocation, and was
    // successfully copied to device.
    assert(in.has_device_allocation());
    assert(!in.host_dirty() && !in.device_dirty());

    // Run the pipeline again into the same output. This variant of
    // realize doesn't do a copy-back.
    f.realize(out);

    // out still has a device allocation, but now it's dirty.
    assert(out.has_device_allocation());
    assert(!out.host_dirty() && out.device_dirty());

    // in has not changed
    assert(in.has_device_allocation());
    assert(!in.host_dirty() && !in.device_dirty());

    printf("Success!\n");
    return 0;
}
