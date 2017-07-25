#include <math.h>
#include <stdio.h>
#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include <assert.h>

#if defined(TEST_CUDA)
#include "HalideRuntimeCuda.h"
#elif defined(TEST_OPENCL)
#include "HalideRuntimeOpenCL.h"
#elif defined(TEST_METAL)
#include "HalideRuntimeMetal.h"
#endif

#include "gpu_object_lifetime.h"

#include "test/common/gpu_object_lifetime_tracker.h"

using namespace Halide::Runtime;

Halide::Internal::GpuObjectLifetimeTracker tracker;

void my_halide_print(void *user_context, const char *str) {
    printf("%s", str);

    tracker.record_gpu_debug(str);
}

int main(int argc, char **argv) {
    halide_set_custom_print(&my_halide_print);

    // Run the whole program several times.
    for (int wrap_memory = 0; wrap_memory < 2; wrap_memory++) {
        // Do an explicit copy-back and device free.
        {
            int scratch[80];
            Buffer<int> output = wrap_memory ? Buffer<int>(scratch, 80) : Buffer<int>(80);

            gpu_object_lifetime(output);

            output.copy_to_host();
            output.device_free();

            for (int x = 0; x < output.width(); x++) {
                if (output(x) != x) {
                    printf("Error! %d != %d\n", output(x), x);
                    return -1;
                }
            }
        }

        // Do an explicit copy-back but no device free
        {
            int scratch[80];
            Buffer<int> output = wrap_memory ? Buffer<int>(scratch, 80) : Buffer<int>(80);

            gpu_object_lifetime(output);

            output.copy_to_host();

            for (int x = 0; x < output.width(); x++) {
                if (output(x) != x) {
                    printf("Error! %d != %d\n", output(x), x);
                    return -1;
                }
            }
        }

        // Do no explicit copy-back and no device free
        {
            int scratch[80];
            Buffer<int> output = wrap_memory ? Buffer<int>(scratch, 80) : Buffer<int>(80);
            gpu_object_lifetime(output);
        }

        // Use a device_release between the two loop iterations to
        // check that it doesn't leak anything.
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
        return ret;
    }

    printf("Success!\n");
    return 0;
}
