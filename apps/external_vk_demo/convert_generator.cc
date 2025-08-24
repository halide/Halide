#include "Halide.h"

using namespace Halide;

// RGB to Grayscale conversion generator for AOT compilation
class ConvertGenerator : public Halide::Generator<ConvertGenerator> {
public:
    Input<Buffer<uint8_t, 3>> input{"input"};
    Output<Buffer<uint8_t, 2>> output{"output"};

    void generate() {
        Var x("x"), y("y");
        input.dim(0).set_stride(3);
        input.dim(2).set_stride(1);
        input.dim(2).set_bounds(0, 3);

        // RGB to Grayscale conversion using standard luminance formula
        output(x, y) = cast<uint8_t>(
            0.299f * cast<float>(input(x, y, 0)) +  // Red
            0.587f * cast<float>(input(x, y, 1)) +  // Green
            0.114f * cast<float>(input(x, y, 2))    // Blue
        );
        // Schedule for target
        // dumb scheduling
        if (get_target().has_feature(Target::Vulkan)) {
            // GPU scheduling for Vulkan
            Var xi("xi"), yi("yi");
            output.gpu_tile(x, y, xi, yi, 16, 16);
        } else {
            // CPU scheduling
            output.vectorize(x, 8);
        }
    }
};

HALIDE_REGISTER_GENERATOR(ConvertGenerator, convert_generator)