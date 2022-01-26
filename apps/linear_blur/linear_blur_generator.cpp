#include "Halide.h"
#include "linear_to_srgb.stub.h"
#include "simple_blur.stub.h"
#include "srgb_to_linear.stub.h"

namespace {

struct LinearBlur : public Halide::Generator<LinearBlur> {
    Input<Buffer<float, 3>> input{"input"};
    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        Var x("x"), y("y"), c("c");

        Func linear = srgb_to_linear::generate(this, {input});
        Func blurred = simple_blur::generate(this, {linear, input.width(), input.height()});
        Func srgb = linear_to_srgb::generate(this, {blurred});
        output(x, y, c) = srgb(x, y, c);

        if (auto_schedule) {
            input.set_estimates({{0, 1536}, {0, 2560}, {0, 4}});
            output.set_estimates({{0, 1536}, {0, 2560}, {0, 4}});
        } else {
            assert(false && "non-auto_schedule not supported.");
            abort();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(LinearBlur, linear_blur)
