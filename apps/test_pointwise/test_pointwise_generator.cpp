#include "Halide.h"

namespace {

class TestPointwise : public Halide::Generator<TestPointwise> {
public:
    Input<Buffer<float>> input{"input", 3};
    Output<Buffer<float>> output{"output", 3};

    void generate() {
        Var x("x"), y("y"), c("c");

        output(x, y, c) = input(x, y, c);

        // Estimates (for autoscheduler; ignored otherwise)
        {
            input.dim(0).set_estimate(0, 1024);
            input.dim(1).set_estimate(0, 1024);
            input.dim(2).set_estimate(0, 3);
            output.dim(0).set_estimate(0, 1024);
            output.dim(1).set_estimate(0, 1024);
            output.dim(2).set_estimate(0, 3);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(TestPointwise, test_pointwise)
