#include "Halide.h"

namespace {

class Simple : public Halide::Generator<Simple> {
public:
    GeneratorParam<int> offset{"offset", 0};
    GeneratorParam<LoopLevel> compute_level{"compute_level", LoopLevel::root()};

    Input<Buffer<uint8_t, 2>> buffer_input{"buffer_input"};
    Input<Func> func_input{"func_input", 2};  // require a 2-dimensional Func but leave Type unspecified
    Input<float> float_arg{"float_arg", 1.0f, 0.0f, 100.0f};

    Output<Func> simple_output{"simple_output", Float(32), 2};

    void generate() {
        simple_output(x, y) = cast<float>(func_input(x, y) + offset + buffer_input(x, y)) + float_arg;
    }

    void schedule() {
        simple_output.compute_at(compute_level);
    }

private:
    Var x{"x"}, y{"y"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Simple, simple)
