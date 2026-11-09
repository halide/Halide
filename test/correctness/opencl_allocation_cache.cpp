#include "Halide.h"

using namespace Halide;

// Checks the cases specific to the OpenCL allocation cache. The general
// behavior of the cache under load is covered by gpu_allocation_cache.

static uintptr_t (*get_cl_mem)(void *, halide_buffer_t *) = nullptr;

struct RawBuffer {
    halide_dimension_t dim = {0, 0, 1};
    halide_buffer_t buf = {};

    explicit RawBuffer(int size) {
        dim.extent = size;
        buf.type = halide_type_of<float>();
        buf.dimensions = 1;
        buf.dim = &dim;
    }

    uintptr_t mem() {
        return get_cl_mem(nullptr, &buf);
    }
};

static void check(int result, const char *what) {
    if (result != 0) {
        printf("%s failed: %d\n", what, result);
        exit(1);
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenCL)) {
        printf("[SKIP] OpenCL not enabled.\n");
        return 0;
    }

    const halide_device_interface_t *iface = get_device_interface_for_device_api(DeviceAPI::OpenCL, target);
    get_cl_mem = (uintptr_t (*)(void *, halide_buffer_t *))
        Internal::JITSharedRuntime::find_symbol(target, "halide_opencl_get_cl_mem");
    if (iface == nullptr || get_cl_mem == nullptr) {
        printf("Failed to get the OpenCL runtime\n");
        return 1;
    }

    Internal::JITSharedRuntime::reuse_device_allocations(true);

    printf("Test that a freed allocation is reused.\n");
    {
        RawBuffer a(1024);
        check(iface->device_malloc(nullptr, &a.buf, iface), "device_malloc");
        uintptr_t mem = a.mem();
        check(iface->device_free(nullptr, &a.buf), "device_free");

        // Drivers may hand out a released cl_mem handle again for the next
        // new allocation, so make an unrelated allocation first.
        RawBuffer other(1024 * 1024);
        check(iface->device_malloc(nullptr, &other.buf, iface), "device_malloc");

        // A slightly smaller request fits in the same allocation.
        RawBuffer b(1000);
        check(iface->device_malloc(nullptr, &b.buf, iface), "device_malloc");
        if (b.mem() != mem) {
            printf("Expected the cached allocation to be reused\n");
            return 1;
        }
        check(iface->device_free(nullptr, &b.buf), "device_free");
        check(iface->device_free(nullptr, &other.buf), "device_free");
    }

    printf("Test that an allocation much larger than the request is not reused.\n");
    {
        RawBuffer a(1024 * 1024);
        check(iface->device_malloc(nullptr, &a.buf, iface), "device_malloc");
        uintptr_t mem = a.mem();
        check(iface->device_free(nullptr, &a.buf), "device_free");

        RawBuffer b(1024);
        check(iface->device_malloc(nullptr, &b.buf, iface), "device_malloc");
        if (b.mem() == mem) {
            printf("A much larger cached allocation should not have been reused\n");
            return 1;
        }
        check(iface->device_free(nullptr, &b.buf), "device_free");
    }

    printf("Test that an allocation with a live crop is not cached.\n");
    {
        RawBuffer a(1024);
        check(iface->device_malloc(nullptr, &a.buf, iface), "device_malloc");
        uintptr_t mem = a.mem();

        RawBuffer crop(512);
        crop.buf.host = nullptr;
        crop.dim.min = 256;
        check(iface->device_crop(nullptr, &a.buf, &crop.buf), "device_crop");
        check(iface->device_free(nullptr, &a.buf), "device_free");

        // The crop still references the allocation, so it must not be
        // handed out again.
        RawBuffer b(1024);
        check(iface->device_malloc(nullptr, &b.buf, iface), "device_malloc");
        if (b.mem() == mem) {
            printf("An allocation with a live crop should not have been reused\n");
            return 1;
        }
        check(iface->device_free(nullptr, &b.buf), "device_free");
        check(iface->device_release_crop(nullptr, &crop.buf), "device_release_crop");
    }

    printf("Test that pipelines produce correct output with the cache.\n");
    {
        Func f;
        Var x, xi;
        f(x) = x * 2;
        f.gpu_tile(x, xi, 16);
        for (int i = 0; i < 10; i++) {
            Buffer<int> out = f.realize({1000 + i * 37});
            out.copy_to_host();
            for (int j = 0; j < out.width(); j++) {
                if (out(j) != j * 2) {
                    printf("out(%d) = %d instead of %d\n", j, out(j), j * 2);
                    return 1;
                }
            }
        }
    }

    // Disabling reuse empties the cache.
    Internal::JITSharedRuntime::reuse_device_allocations(false);

    printf("Test that nothing is cached when reuse is disabled.\n");
    {
        RawBuffer a(1024);
        check(iface->device_malloc(nullptr, &a.buf, iface), "device_malloc");
        check(iface->device_free(nullptr, &a.buf), "device_free");
        iface->device_release(nullptr, iface);
    }

    printf("Success!\n");
    return 0;
}
