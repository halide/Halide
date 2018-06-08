#include "Halide.h"

namespace {

class BitOperations : public Halide::Generator<BitOperations> {
public:
    Input<Buffer<uint64_t>>  input{"input", 1};
    Output<Buffer<uint8_t>> output{"output", 1};

    void generate() {
        Var x;
        output(x) = Halide::cast<uint8_t>(count_leading_zeros(input(x)));
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(BitOperations, bit_operations)
