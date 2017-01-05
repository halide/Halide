#include <math.h>
#include <stdio.h>
#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include <assert.h>

#if COMPILING_FOR_CUDA
#include "HalideRuntimeCuda.h"
#elif COMPILING_FOR_OPENCL
#include "HalideRuntimeOpenCL.h"
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
    for (int i = 0; i < 2; i++) {
        Buffer<int> output(80);

        gpu_object_lifetime(output);

        output.copy_to_host();
        output.device_free();

        for (int x = 0; x < output.width(); x++) {
            if (output(x) != x) {
                printf("Error! %d != %d\n", output(x), x);
                return -1;
            }
        }

#if COMPILING_FOR_CUDA
        halide_device_release(nullptr, halide_cuda_device_interface());
#elif COMPILING_FOR_OPENCL
        halide_device_release(nullptr, halide_opencl_device_interface());
#endif
    }

    int ret = tracker.validate_gpu_object_lifetime(false /* allow_globals */, true /* allow_none */, 1 /* max_globals */);
    if (ret != 0) {
        return ret;
    }

    printf("Success!\n");
    return 0;
}
