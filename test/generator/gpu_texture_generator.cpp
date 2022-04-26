#include "Halide.h"

namespace {

class GpuTexture : public Halide::Generator<GpuTexture> {
public:
    Input<Buffer<int32_t, 2>> input{"input"};
    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        Var x("x"), y("y");

        // Create a simple pipeline that scales pixel values by 2.
        output(x, y) = input(x, y) * 2;

        if (get_target().has_feature(Target::OpenCL)) {
            input.store_in(MemoryType::GPUTexture);
            output.store_in(MemoryType::GPUTexture);
        }

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            output.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(GpuTexture, gpu_texture)
