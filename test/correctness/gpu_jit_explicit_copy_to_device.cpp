#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (!target.has_gpu_feature() && !target.has_feature(Target::OpenGLCompute)) {
        printf("This is a gpu-specific test. Skipping it\n");
        return 0;
    }

    // We'll have two input buffers. For one we'll copy to the device
    // explicitly. For the other we'll do a device malloc and set
    // host_dirty.
    {
        Buffer<float> a(100, 100), b(100, 100);

        assert(!a.host_dirty());
        a.fill(2.0f);
        assert(!a.has_device_allocation());
        assert(a.host_dirty());
        a.copy_to_device();
        assert(a.has_device_allocation());
        assert(!a.host_dirty());

        assert(!b.host_dirty());
        b.fill(3.0f);
        assert(!b.has_device_allocation());
        assert(b.host_dirty());
        b.device_malloc();
        assert(b.has_device_allocation());
        assert(b.host_dirty());

        Func f;
        Var x, y;
        f(x, y) = a(x, y) + b(x, y) + 2;
        f.gpu_tile(x, y, 8, 8);

        Buffer<float> out = f.realize(100, 100);
    }

    if (target.has_feature(Target::CUDA)) {
        // Now let's try again using an explicit device API

        Buffer<float> a(100, 100), b(100, 100);

        assert(!a.host_dirty());
        a.fill(2.0f);
        assert(!a.has_device_allocation());
        assert(a.host_dirty());
        a.copy_to_device(DeviceAPI::Cuda);
        assert(a.has_device_allocation());
        assert(!a.host_dirty());

        assert(!b.host_dirty());
        b.fill(3.0f);
        assert(!b.has_device_allocation());
        assert(b.host_dirty());
        b.device_malloc(DeviceAPI::Cuda);
        assert(b.has_device_allocation());
        assert(b.host_dirty());

        Func f;
        Var x, y;
        f(x, y) = a(x, y) + b(x, y) + 2;
        f.gpu_tile(x, y, 8, 8, DeviceAPI::Cuda);

        Buffer<float> out = f.realize(100, 100);
    }

    // Here's a wart: b was copied to the device, but it still has
    // host_dirty set, because it is a *copy* of b's buffer_t that is
    // held by the pipeline, and this copy was passed to
    // copy_to_device. It's hard to fix this without keeping
    // references to user Buffers (which may leave scope before the
    // pipeline does!).
    assert(b.host_dirty()); // :(

    out.for_each_value([&](float f) {
        if (f != 7.0f) {
            printf("%f != 4.0f\n", f);
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
