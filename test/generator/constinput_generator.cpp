#include "Halide.h"

namespace {

class ConstInput : public Halide::Generator<ConstInput> {
public:
    Input<Buffer<int32_t>>  input1{ "input1", 3 };
    Input<Buffer<int32_t>>  input2{ "input2", 3 };
    Input<int32_t>          scalar{ "scalar", 0 };

    Output<Buffer<int32_t>> output{ "output", 3 };

    void generate() {
        Var x, y, c;
        output(x, y, c) = input1(x, y, c) + input2(x, y, c) + scalar;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConstInput, constinput)
