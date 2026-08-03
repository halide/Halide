#include "Halide.h"

using namespace Halide;

// A simple elementwise kernel with a GPU schedule, used to check that
// halide.runtime can load and run a kernel compiled for a GPU target (the AOT
// artifact bundles its own device-capable Halide runtime). Falls back to a CPU
// schedule when compiled without a GPU feature.
class GpuAdd : public Generator<GpuAdd> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Input<int32_t> offset{"offset"};
    Output<Buffer<uint8_t, 2>> output{"output"};

    Var x, y, xo, yo, xi, yi;

    void generate() {
        output(x, y) = cast<uint8_t>(input(x, y) + offset);
    }

    void schedule() {
        if (get_target().has_gpu_feature()) {
            output.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        }
    }
};

HALIDE_REGISTER_GENERATOR(GpuAdd, gpuadd)
