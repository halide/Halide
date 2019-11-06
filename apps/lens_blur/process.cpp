#include <cstdio>
#include <chrono>

#include "lens_blur.h"
#include "lens_blur_classic_auto_schedule.h"
#include "lens_blur_auto_schedule.h"
#include "bilateral_grid_simple_auto_schedule.h"

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png slices focus_depth blur_radius_scale aperture_samples timing_iterations output.png\n"
               "e.g.: ./process input.png 32 13 0.5 32 3 output.png\n");
        return 0;
    }

    // Let the Halide runtime hold onto GPU allocations for
    // intermediates and reuse them instead of eagerly freeing
    // them. cuMemAlloc/cuMemFree is slower than the algorithm!
    halide_reuse_device_allocations(nullptr, true);

    Buffer<uint8_t> left_im = load_image(argv[1]);
    Buffer<uint8_t> right_im = load_image(argv[1]);
    uint32_t slices = atoi(argv[2]);
    uint32_t focus_depth = atoi(argv[3]);
    float blur_radius_scale = atof(argv[4]);
    uint32_t aperture_samples = atoi(argv[5]);
    Buffer<float> output(left_im.width(), left_im.height(), 3);

    // Timing code
    multi_way_bench({
        {"Manual", [&]() { lens_blur(left_im, right_im, slices, focus_depth, blur_radius_scale, aperture_samples, output); output.device_sync(); }},
        {"Classic auto-scheduled", [&]() { lens_blur_classic_auto_schedule(left_im, right_im, slices, focus_depth, blur_radius_scale, aperture_samples, output); output.device_sync(); }},
        {"Auto-scheduled", [&]() { lens_blur_auto_schedule(left_im, right_im, slices, focus_depth, blur_radius_scale, aperture_samples, output); output.device_sync(); }},
        {"Simple auto-scheduled", [&]() { lens_blur_simple_auto_schedule(left_im, right_im, slices, focus_depth, blur_radius_scale, aperture_samples, output); output.device_sync(); }}
        }
    );

    convert_and_save_image(output, argv[7]);

    return 0;
}
