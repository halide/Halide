#include <cassert>
#include <cstdio>
#include <regex>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "iir_blur.h"
#include "iir_blur_auto_schedule.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

namespace {

enum DeviceState {
    USING_METAL_OR_OPENCL,
    NOT_METAL_OR_OPENCL,
    METADATA_ABSENT,
};
DeviceState ensure_cuda_device() {
    const auto hl_target = iir_blur_auto_schedule_metadata()->target;
    if (hl_target == nullptr) {
        printf("Warning: variable *_metadata()->target not specified. "
               "Proceeding to the tests...\n");
        return METADATA_ABSENT;
    }

    if (std::regex_search(hl_target, std::regex{"metal|opencl"})) {
        // note(antonysigma): Error messages if we don't skip the test:
        //
        // OpenCL error: clFinish timeout.
        //
        // Metal: copy_to_host() failed. Error
        // Domain=MTLCommandBufferErrorDomain Code=2 "Caused GPU Timeout Error
        // (00000002:kIOAccelCommandBufferCallbackErrorTimeout)"
        // UserInfo={NSLocalizedDescription=Caused GPU Timeout Error
        // (00000002:kIOAccelCommandBufferCallbackErrorTimeout)}
        printf("[SKIP] Mullapudi2016 experimental GPU schedule "
               "generates copy_to_host() function calls that timeout. "
               "Target = %s. Skipping...\n",
               hl_target);

        return USING_METAL_OR_OPENCL;
    }

    return NOT_METAL_OR_OPENCL;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    if (ensure_cuda_device() == USING_METAL_OR_OPENCL) {
        return 0;
    }

    Halide::Runtime::Buffer<float, 3> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<float, 3> output(input.width(), input.height(), input.channels());

    double best_manual = benchmark([&]() {
        iir_blur(input, 0.5f, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    double best_auto = benchmark([&]() {
        iir_blur_auto_schedule(input, 0.5f, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");
    return 0;
}
