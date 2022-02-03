#include "Halide.h"

#if HALIDE_PREFER_G2_GENERATORS

namespace {

using namespace Halide;

Pipeline BitOperations(Func input8, Func input16, Func input32, Func input64) {
    Var x;
    Func output8, output16, output32, output64;
    output8(x) = Halide::cast<uint8_t>(count_leading_zeros(input8(x)));
    output16(x) = Halide::cast<uint8_t>(count_leading_zeros(input16(x)));
    output32(x) = Halide::cast<uint8_t>(count_leading_zeros(input32(x)));
    output64(x) = Halide::cast<uint8_t>(count_leading_zeros(input64(x)));
    return {{output8, output16, output32, output64}};
}

}  // namespace

HALIDE_REGISTER_G2(
    BitOperations,  // actual C++ fn
    bit_operations,  // build-system name
    Input("input8", UInt(8), 1),
    Input("input16", UInt(16), 1),
    Input("input32", UInt(32), 1),
    Input("input64", UInt(64), 1),
    Output("output8", UInt(8), 1),
    Output("output16", UInt(16), 1),
    Output("output32", UInt(32), 1),
    Output("output64", UInt(64), 1))

#else

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

#endif
