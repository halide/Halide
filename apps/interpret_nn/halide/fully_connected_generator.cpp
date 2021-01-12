#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace interpret_nn {

class FullyConnected : public Generator<FullyConnected> {
public:
    // Input buffers.
    Input<Buffer<uint8_t>> input_{"input", 2};
    Input<Buffer<uint8_t>> weights_{"weights", 2};
    Input<Buffer<int32_t>> bias_{"bias", 1};

    Input<uint8_t> input_offset_{"input_offset"};
    Input<uint8_t> filter_offset_{"filter_offset"};

    // Offset, quantization multiplier and shift for the output.
    Input<int32_t> output_offset_{"output_offset"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<int32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        Var c("c"), b("b");

        Func input_zeroed("input_zeroed");
        Func weights_zeroed("weights_zeroed");
        input_zeroed(c, b) = i16(input_(c, b)) - i16(input_offset_);
        weights_zeroed(c, b) = i16(weights_(c, b)) - i16(filter_offset_);

        RDom rc(weights_.dim(0).min(), weights_.dim(0).extent());
        Func multiplied("multiplied");
        multiplied(c, b) = bias_(c);
        // TODO: I don't think this is quite right vs. tflite implementation.
        // Recheck carefully.
        multiplied(c, b) += i32(weights_zeroed(rc, c)) * i32(input_zeroed(c, b));

        // Saturate and narrow the output.
        Expr output =
            multiply_quantized(multiplied(c, b), output_multiplier_, output_shift_) + output_offset_;
        output_(c, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule.
        require_same_min_extent(1, input_, output_);
        require_same_min_extent(0, input_, weights_);
        require_same_min_extent(0, bias_, output_);

        // TODO: Schedule.
        output_.compute_root();
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::FullyConnected, FullyConnected)
