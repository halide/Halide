#include <math.h>
#include <stdio.h>
#include "HalideRuntime.h"
#include <assert.h>

#if COMPILING_FOR_CUDA
#include <HalideRuntimeCuda.h>
#elif COMPILING_FOR_OPENCL
#include <HalideRuntimeOpenCL.h>
#endif

#include "gpu_object_lifetime.h"
#include "static_image.h"
#include "../common/gpu_object_lifetime.h"

extern "C" void halide_print(void *user_context, const char *str) {
    printf("%s", str);

    record_gpu_debug(str);
}

int main(int argc, char **argv) {

    // Run the whole program several times.
    for (int i = 0; i < 2; i++) {
        Image<int> output(80);

        gpu_object_lifetime(output);

        output.copy_to_host();
        output.dev_free();

        for (int x = 0; x < output.width(); x++) {
            if (output(x) != x) {
                printf("Error! %d != %d\n", output(x), x);
                return -1;
            }
        }

#if COMPILING_FOR_CUDA
        halide_device_release(NULL, halide_cuda_device_interface());
#elif COMPILING_FOR_OPENCL
        halide_device_release(NULL, halide_opencl_device_interface());
#endif
    }

    int ret = validate_gpu_object_lifetime(false /* allow_globals */, true /* allow_none */, 1 /* max_globals */);
    if (ret != 0) {
        return ret;
    }

    printf("Success!\n");
    return 0;
}
