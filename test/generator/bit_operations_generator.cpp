#include "Halide.h"

namespace {

class BitOperations : public Halide::Generator<BitOperations> {
public:
    Input<Buffer<uint8_t>> input8{"input8", 1};
    Input<Buffer<uint16_t>> input16{"input16", 1};
    Input<Buffer<uint32_t>> input32{"input32", 1};
    Input<Buffer<uint64_t>> input64{"input64", 1};

    Output<Buffer<uint8_t>> output8{"output8", 1};
    Output<Buffer<uint8_t>> output16{"output16", 1};
    Output<Buffer<uint8_t>> output32{"output32", 1};
    Output<Buffer<uint8_t>> output64{"output64", 1};

    void generate() {
        Var x;
        output8(x) = Halide::cast<uint8_t>(count_leading_zeros(input8(x)));
        output16(x) = Halide::cast<uint8_t>(count_leading_zeros(input16(x)));
        output32(x) = Halide::cast<uint8_t>(count_leading_zeros(input32(x)));
        output64(x) = Halide::cast<uint8_t>(count_leading_zeros(input64(x)));
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(BitOperations, bit_operations)
