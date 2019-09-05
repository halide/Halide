#if defined(TEST_OPENCL)
#include <stdio.h>
#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "HalideRuntimeOpenCL.h"
#include "opencl_program_caching.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    printf("TEST_OPENCL enabled for opencl_program_caching testing...\n");
    halide_opencl_set_compiled_programs_cache_dir(".");

    Buffer<int> output = Buffer<int>(80);

    opencl_program_caching(output);

    output.copy_to_host();
    output.device_free();

    for (int x = 0; x < output.width(); x++) {
        if (output(x) != x) {
            printf("Error! %d != %d\n", output(x), x);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
#else
#include <stdio.h>
int main(int argc, char **argv) {
    printf("Success!\n");
    return 0;
}
#endif
