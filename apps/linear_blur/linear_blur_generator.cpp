#include "Halide.h"
#include "linear_to_srgb.stub.h"
#include "srgb_to_linear.stub.h"
#include "simple_blur.stub.h"

namespace {

struct LinearBlur : public Halide::Generator<LinearBlur> {
    Input<Buffer<float>>  input{"input", 3};
    Output<Buffer<float>> output{"output", 3};

    void generate() {
        Var x("x"), y("y"), c("c");

        Func linear = srgb_to_linear::generate(this, {input});
        Func blurred = simple_blur::generate(this, {linear, input.width(), input.height()});
        Func srgb = linear_to_srgb::generate(this, {blurred});
        output(x, y, c) = srgb(x, y, c);

        if (auto_schedule) {
            input.dim(0).set_bounds_estimate(0, 1536)
                 .dim(1).set_bounds_estimate(0, 2560)
                 .dim(2).set_bounds_estimate(0, 4);
            output.estimate(x, 0, 1536)
                  .estimate(y, 0, 2560)
                  .estimate(c, 0, 4);
            // TODO(srj): set_bounds_estimate should work for Output<Buffer<>>, but does not
            // output.dim(0).set_bounds_estimate(0, 1536)
            //       .dim(1).set_bounds_estimate(0, 2560)
            //       .dim(2).set_bounds_estimate(0, 4);
        } else {
            assert(false && "non-auto_schedule not supported.");
            abort();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(LinearBlur, linear_blur)
