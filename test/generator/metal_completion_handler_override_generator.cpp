#include "Halide.h"

namespace {

class SimpleMetalPipeline : public Halide::Generator<SimpleMetalPipeline> {
public:
    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        Var x("x"), y("y");

        // Create a simple pipeline that scales pixel values by 2.
        output(x, y) = x + y * 2;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            output.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SimpleMetalPipeline, metal_completion_handler_override)