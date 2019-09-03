#include "Halide.h"

namespace {

class OpenClProgramCaching : public Halide::Generator<OpenClProgramCaching> {
public:
    Output<Buffer<int32_t>> output{"output", 1};

    void generate() {
        Var x;

        output(x) = x;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, xi;
            output.gpu_tile(x, xo, xi, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(OpenClProgramCaching, opencl_program_caching)
