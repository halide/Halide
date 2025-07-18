#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <regex>

#include "local_laplacian.h"
#ifndef NO_AUTO_SCHEDULE
#include "local_laplacian_auto_schedule.h"
#endif

#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

namespace {

enum DeviceState {
    IS_CUDA,
    NOT_CUDA,
    ENV_VARIABLE_ABSENT,
};
DeviceState ensure_cuda_device() {
    const auto hl_target = std::getenv("HL_TARGET");
    if (hl_target == nullptr) {
        printf("Warning: Environment variable HL_TARGET not specified. "
               "Proceeding to the tests...\n");
        return ENV_VARIABLE_ABSENT;
    }

    if (std::regex_search(hl_target, std::regex{"metal|vulkan|opencl"})) {
        // note(antonysigma): Error messages if we don't skip the test:
        //
        // OpenCL error: CL_INVALID_WORK_GROUP_SIZE clEnqueueNDRangeKernel
        // failed
        //
        // 2025-07-17 17:24:32.170 local_laplacian_process[63513:6587844] Metal
        // API Validation Enabled -[MTLDebugComputeCommandEncoder
        // _validateThreadsPerThreadgroup:]:1266: failed assertion
        // `(threadsPerThreadgroup.width(62) * threadsPerThreadgroup.height(32)
        // * threadsPerThreadgroup.depth(1))(1984) must be <= 1024. (device
        // threadgroup size limit)'
        //
        // Vulkan: vkQueueWaitIdle returned VK_ERROR_DEVICE_LOST
        printf("[SKIP] Mullapudi2016 experimental GPU schedule "
               "generates the gpu_threads where thread count per block "
               "is not an multiple of 32. Target = %s. Skipping...\n",
               hl_target);

        return NOT_CUDA;
    }

    return IS_CUDA;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png levels alpha beta timing_iterations output.png\n"
               "e.g.: ./process input.png 8 1 1 10 output.png\n");
        return 1;
    }

    if (ensure_cuda_device() == NOT_CUDA) {
        return 0;
    }

    // Input may be a PNG8
    Buffer<uint16_t, 3> input = load_and_convert_image(argv[1]);

    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);
    Buffer<uint16_t, 3> output(input.width(), input.height(), 3);
    int timing = atoi(argv[5]);

    local_laplacian(input, levels, alpha / (levels - 1), beta, output);

    // Timing code

    // Manually-tuned version
    double best_manual = benchmark(timing, 1, [&]() {
        local_laplacian(input, levels, alpha / (levels - 1), beta, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

#ifndef NO_AUTO_SCHEDULE
    // Auto-scheduled version
    double best_auto = benchmark(timing, 1, [&]() {
        local_laplacian_auto_schedule(input, levels, alpha / (levels - 1), beta, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);
#endif

    convert_and_save_image(output, argv[6]);

    printf("Success!\n");
    return 0;
}
