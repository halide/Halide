#include "Halide.h"

namespace {

class GpuAdd : public Halide::Generator<GpuAdd> {
public:
    Input<Buffer<int32_t>> input{"input", 2};

    Output<Buffer<int32_t>> output{"output", 2};

    void generate() {
        Var x("x"), y("y");

        // Create a simple pipeline that scales pixel values by 2.
        output(x, y) = input(x, y) + 2;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            output.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        }
    }
};

class GpuMul : public Halide::Generator<GpuMul> {
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

HALIDE_REGISTER_GENERATOR(GpuAdd, gpu_multi_add)
HALIDE_REGISTER_GENERATOR(GpuMul, gpu_multi_mul)
