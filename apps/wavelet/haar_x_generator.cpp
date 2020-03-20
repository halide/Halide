#include "Halide.h"

#include "daubechies_constants.h"

namespace {

Halide::Var x("x"), y("y"), c("c");

class haar_x : public Halide::Generator<haar_x> {
public:
    Input<Buffer<float>> in_{"in", 2};
    Output<Buffer<float>> out_{"out", 3};

    void generate() {
        Func in = Halide::BoundaryConditions::repeat_edge(in_);

        out_(x, y, c) = mux(c,
                            {(in(2 * x, y) + in(2 * x + 1, y)),
                             (in(2 * x, y) - in(2 * x + 1, y))}) /
                        2;
        out_.unroll(c, 2);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(haar_x, haar_x)
