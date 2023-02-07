#include "Halide.h"

namespace {

using namespace Halide;

class Add : public Halide::Generator<Add> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Input<uint8_t> offset{"offset"};
    Output<Buffer<uint8_t, 2>> output{"output"};

    void generate() {
        // Algorithm
        output(x, y) = input(x, y) + offset;  // Don't care overflow/saturation
    }

    void schedule() {
        input.set_estimates({{0, 256}, {0, 426}});
        output.set_estimates({{0, 256}, {0, 426}});

        if (!using_autoscheduler()) {
            // NOTE: In baremetal, .parallel() doesn't make sense as thread support is unavailable.

            const int vector_width = get_target().natural_vector_size(output.types()[0]);
            output
                .compute_root()
                .vectorize(x, vector_width);
        }
    }

    Var x{"x"}, y{"y"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Add, add)
