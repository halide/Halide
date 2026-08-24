#include "Halide.h"

using namespace Halide;

// A simple elementwise kernel with a GPU schedule, used to check that
// halide.runtime can load and run a kernel compiled for a GPU target (the AOT
// artifact bundles its own device-capable Halide runtime).
class GpuAdd : public Generator<GpuAdd> {
public:
    Input<Buffer<uint32_t, 2>> input{"input"};
    Input<uint32_t> offset{"offset"};
    Output<Buffer<uint32_t, 2>> output{"output"};

    Var x, y, xo, yo, xi, yi;

    void generate() {
        output(x, y) = input(x, y) + offset;
    }

    void schedule() {
        if (get_target().has_gpu_feature()) {
            output.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        }
    }
};

HALIDE_REGISTER_GENERATOR(GpuAdd, gpuadd)
