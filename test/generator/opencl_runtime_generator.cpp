#include "Halide.h"

namespace {

class OpenCLRuntime : public Halide::Generator<OpenCLRuntime> {
public:
    Output<Buffer<int32_t>>  output{"output", 2};

    void generate() {
        output(Halide::_, Halide::_) = 0;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(OpenCLRuntime, opencl_runtime)
