#include "Halide.h"

namespace {

class OpenCL_Runtime : public Halide::Generator<OpenCL_Runtime> {
public:
    Input<Buffer<int32_t>> input{"input", 2};

    Output<Buffer<int32_t>> output{"output", 2};

    void generate() {
        Var x("x"), y("y");

        // Create a simple pipeline that scales pixel values by 2.
        output(x, y) = input(x, y) * 2;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            output.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(OpenCL_Runtime, opencl_runtime)
