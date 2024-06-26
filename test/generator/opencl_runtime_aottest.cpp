#include "HalideRuntime.h"
#if defined(TEST_OPENCL)
#include "HalideRuntimeOpenCL.h"
#endif

#include <cstdio>
#include <string>

int main(int argc, char **argv) {
#if defined(TEST_OPENCL)
    std::string platform_name = "custom_platform";
    halide_opencl_set_platform_name(platform_name.c_str());
    if (platform_name != halide_opencl_get_platform_name(nullptr)) {
        printf("Value returned from halide_opencl_get_platform_name doesn't match\n");
        return 1;
    }

    std::string device_type = "custom_device";
    halide_opencl_set_device_type(device_type.c_str());
    if (device_type != halide_opencl_get_device_type(nullptr)) {
        printf("Value returned from halide_opencl_get_device_type doesn't match\n");
        return 1;
    }

    std::string build_options = "-c ustom_build_option";
    halide_opencl_set_build_options(build_options.c_str());
    if (build_options != halide_opencl_get_build_options(nullptr)) {
        printf("Value returned from halide_opencl_get_build_options doesn't match\n");
        return 1;
    }

    printf("Success!\n");
#else
    printf("[SKIP] Test requires OpenCL.\n");
#endif
    return 0;
}
