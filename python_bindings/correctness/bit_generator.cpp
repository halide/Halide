#include "Halide.h"

using namespace Halide;

class BitGenerator : public Halide::Generator<BitGenerator> {
public:
    Input<Buffer<bool>> bit_input{"input_uint1", 1};
    Input<bool> bit_constant{"constant_uint1"};

    Output<Buffer<bool>> bit_output{"output_uint1", 1};

    Var x, y, z;

    void generate() {
        bit_output(x) = bit_input(x) + bit_constant;
    }

    void schedule() {
    }
};

HALIDE_REGISTER_GENERATOR(BitGenerator, bit)
