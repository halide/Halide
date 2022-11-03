#include "Halide.h"

namespace {

class SimpleCpp : public Halide::Generator<SimpleCpp> {
public:
    GeneratorParam<int> offset{"offset", 0};

    Input<Buffer<uint8_t, 2>> buffer_input{"buffer_input"};
    Input<float> float_arg{"float_arg", 1.0f, 0.0f, 100.0f};

    Output<Buffer<float, 2>> simple_output{"simple_output"};

    void generate() {
        simple_output(x, y) = cast<float>(offset + buffer_input(x, y)) + float_arg;
    }

    void schedule() {
        simple_output.compute_root();
    }

private:
    Var x{"x"}, y{"y"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SimpleCpp, simplecpp)
