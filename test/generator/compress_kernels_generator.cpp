#include "Halide.h"

namespace {

class CompressKernels : public Halide::Generator<CompressKernels> {
public:
    Input<Buffer<int32_t, 2>> input{"input"};
    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        Var x("x"), y("y");

        Func f("f");
        f(x, y) = input(x, y) * 2;
        output(x, y) = f(x, y) + 3;

        if (get_target().has_gpu_feature()) {
            Var xo, yo, xi, yi;
            f.compute_root().gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
            output.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(CompressKernels, compress_kernels)
