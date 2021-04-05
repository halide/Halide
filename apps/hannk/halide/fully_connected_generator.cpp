#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace hannk {

class FullyConnected : public Generator<FullyConnected> {
public:
    Input<Buffer<uint8_t>> input_{"input", 2};
    Input<Buffer<uint8_t>> filter_{"filter", 2};
    Input<Buffer<int32_t>> bias_{"bias", 1};

    Input<uint8_t> input_zero_{"input_zero"};
    Input<uint8_t> filter_zero_{"filter_zero"};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        Var c("c"), b("b");

        Func input_zeroed("input_zeroed");
        Func filter_zeroed("filter_zeroed");
        input_zeroed(c, b) = i16(input_(c, b)) - i16(input_zero_);
        filter_zeroed(c, b) = i16(filter_(c, b)) - i16(filter_zero_);

        RDom rc(filter_.dim(0).min(), filter_.dim(0).extent());
        Func multiplied("multiplied");
        multiplied(c, b) = bias_(c);
        multiplied(c, b) += i32(filter_zeroed(rc, c)) * i32(input_zeroed(rc, b));

        // Saturate and narrow the output.
        Expr output =
            multiply_quantized(multiplied(c, b), output_multiplier_, output_shift_);
        output = saturating_add(i16_sat(output), output_zero_);
        output_(c, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule.
        require_same_min_extent(1, input_, output_);
        require_same_min_extent(0, input_, filter_);
        require_same_min_extent(0, bias_, output_);

        // TODO: Schedule.
        output_.compute_root();
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::FullyConnected, FullyConnected)
