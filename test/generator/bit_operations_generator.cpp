#include "Halide.h"

namespace {

class BitOperations : public Halide::Generator<BitOperations> {
public:
    Input<Buffer<uint8_t, 1>> input8{"input8"};
    Input<Buffer<uint16_t, 1>> input16{"input16"};
    Input<Buffer<uint32_t, 1>> input32{"input32"};
    Input<Buffer<uint64_t, 1>> input64{"input64"};

    Output<Buffer<uint8_t, 1>> output8{"output8"};
    Output<Buffer<uint8_t, 1>> output16{"output16"};
    Output<Buffer<uint8_t, 1>> output32{"output32"};
    Output<Buffer<uint8_t, 1>> output64{"output64"};

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
