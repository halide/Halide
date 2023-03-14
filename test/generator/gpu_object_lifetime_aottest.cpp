#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#if defined(TEST_CUDA)
#include "HalideRuntimeCuda.h"
#elif defined(TEST_OPENCL)
#include "HalideRuntimeOpenCL.h"
#elif defined(TEST_METAL)
#include "HalideRuntimeMetal.h"
#endif

#include "gpu_object_lifetime.h"

#include "gpu_object_lifetime_tracker.h"

using namespace Halide::Runtime;

Halide::Internal::GpuObjectLifetimeTracker tracker;

void my_halide_print(void *user_context, const char *str) {
    printf("%s", str);

    tracker.record_gpu_debug(str);
}

int main(int argc, char **argv) {

#if defined(TEST_CUDA)
    printf("TEST_CUDA enabled for gpu_object_lifetime testing...\n");
#elif defined(TEST_OPENCL)
    printf("TEST_OPENCL enabled for gpu_object_lifetime testing...\n");
#elif defined(TEST_METAL)
    printf("TEST_METAL enabled for gpu_object_lifetime testing...\n");
#else
    printf("[SKIP] No GPU features enabled for gpu_object_lifetime testing!\n");
    return 0;
#endif

    halide_set_custom_print(&my_halide_print);

    // Run the whole program several times.
    for (int wrap_memory = 0; wrap_memory < 2; wrap_memory++) {
        // Do an explicit copy-back and device free.
        {
            int scratch[80];
            Buffer<int, 1> output = wrap_memory ? Buffer<int, 1>(scratch, 80) : Buffer<int, 1>(80);

            gpu_object_lifetime(output);

            output.copy_to_host();
            output.device_free();

            for (int x = 0; x < output.width(); x++) {
                if (output(x) != x) {
                    printf("Error! (explicit copy back %d): %d != %d\n", wrap_memory, output(x), x);
                    return 1;
                }
            }
        }

        // Do an explicit copy-back but no device free
        {
            int scratch[80];
            Buffer<int, 1> output = wrap_memory ? Buffer<int, 1>(scratch, 80) : Buffer<int, 1>(80);

            gpu_object_lifetime(output);

            output.copy_to_host();

            for (int x = 0; x < output.width(); x++) {
                if (output(x) != x) {
                    printf("Error! (explicit copy back, no device free %d): %d != %d\n", wrap_memory, output(x), x);
                    return 1;
                }
            }
        }

        // Do no explicit copy-back and no device free
        {
            int scratch[80];
            Buffer<int, 1> output = wrap_memory ? Buffer<int, 1>(scratch, 80) : Buffer<int, 1>(80);
            gpu_object_lifetime(output);
        }

        // Test coverage for Halide::Runtime::Buffer device pointer management.
        {
            Buffer<int, 1> output(80);

            // Call Halide filter to get a device allocation.
            gpu_object_lifetime(output);

            {
                // Construct a new buffer from the halide_buffer_t and let it destruct.
                // Verifies this does not deallocate or otherwise disable the device handle.
                Buffer<int, 1> temp(*output.raw_buffer());
            }
            output.copy_to_host();
        }

        // Do this test twice to test explicit unwrapping and letting the destructor do it.
        for (int i = 0; i < 2; i++) {
            Buffer<int, 1> output(80);

            // Call Halide filter to get a device allocation.
            gpu_object_lifetime(output);

            // This is ugly. Getting a native device handle from scratch requires writing API
            // dependent code. Instead, we reuse a Halide allocated handle from an API where we know
            // the device field is just a raw device handle. If we don't know this about the API,
            // we don't test anything here.
            // This gets some minimal test coverage for code paths in Halide::Runtime::Buffer.
            bool can_rewrap = false;
            uintptr_t native_handle = 0;

#if defined(TEST_CUDA)
            if (output.raw_buffer()->device_interface == halide_cuda_device_interface()) {
                native_handle = output.raw_buffer()->device;
                can_rewrap = true;
            }
#elif defined(TEST_OPENCL)
            if (output.raw_buffer()->device_interface == halide_opencl_device_interface()) {
                native_handle = halide_opencl_get_cl_mem(nullptr, output.raw_buffer());
                can_rewrap = true;
            }
#elif defined(TEST_METAL)
            if (output.raw_buffer()->device_interface == halide_metal_device_interface()) {
                native_handle = halide_metal_get_buffer(nullptr, output.raw_buffer());
                can_rewrap = true;
            }
#endif

            if (can_rewrap) {
                Buffer<int, 1> wrap_test(80);
                wrap_test.device_wrap_native(output.raw_buffer()->device_interface, native_handle);
                wrap_test.set_device_dirty();
                wrap_test.copy_to_host();
                output.copy_to_host();

                for (int x = 0; x < output.width(); x++) {
                    if (output(x) != wrap_test(x)) {
                        printf("Error! (wrap native test %d): %d != %d\n", i, output(x), wrap_test(x));
                        return 1;
                    }
                }
                if (i == 1) {
                    wrap_test.device_detach_native();
                }
            }
        }

        // Test coverage for Halide::Runtime::Buffer construction from halide_buffer_t, unmanaged
        {
            Buffer<int, 1> output(80);
            halide_buffer_t raw_buf = *output.raw_buffer();

            // Call Halide filter to get a device allocation.
            gpu_object_lifetime(&raw_buf);

            {
                Buffer<int, 1> copy(raw_buf);
            }
            // Note that a nonzero result should be impossible here (in theory)
            int result = halide_device_free(nullptr, &raw_buf);
            if (result != 0) {
                printf("Error! halide_device_free() returned: %d\n", result);
                return 1;
            }
        }

        // Test coverage for Halide::Runtime::Buffer construction from halide_buffer_t, taking ownership
        {
            Buffer<int, 1> output(80);
            halide_buffer_t raw_buf = *output.raw_buffer();

            // Call Halide filter to get a device allocation.
            gpu_object_lifetime(&raw_buf);

            Buffer<int, 1> copy(raw_buf, Halide::Runtime::BufferDeviceOwnership::Allocated);
        }

        // Test combined device and host allocation support.
        {
            Buffer<int, 1> output(80);
            gpu_object_lifetime(output);
            if (output.raw_buffer()->device_interface != nullptr) {
                Buffer<int, 1> output2(nullptr, 80);
                output2.device_and_host_malloc(output.raw_buffer()->device_interface);
                gpu_object_lifetime(output2);

                output.copy_to_host();
                output2.copy_to_host();

                for (int x = 0; x < output.width(); x++) {
                    if (output(x) != output2(x)) {
                        printf("Error! (device and host allocation test): %d != %d\n", output(x), output2(x));
                        return 1;
                    }
                }
            }
        }

#if defined(TEST_CUDA)
        halide_device_release(nullptr, halide_cuda_device_interface());
#elif defined(TEST_OPENCL)
        halide_device_release(nullptr, halide_opencl_device_interface());
#elif defined(TEST_METAL)
        halide_device_release(nullptr, halide_metal_device_interface());
#endif
    }

    int ret = tracker.validate_gpu_object_lifetime(false /* allow_globals */, true /* allow_none */, 2 /* max_globals */);
    if (ret != 0) {
        fprintf(stderr, "validate_gpu_object_lifetime() failed\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
