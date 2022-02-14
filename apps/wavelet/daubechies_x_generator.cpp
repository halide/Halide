#include "Halide.h"

#include "daubechies_constants.h"

namespace {

Halide::Var x("x"), y("y"), c("c");

class daubechies_x : public Halide::Generator<daubechies_x> {
public:
    Input<Buffer<float, 2>> in_{"in"};
    Output<Buffer<float, 3>> out_{"out"};

    void generate() {
        Func in = Halide::BoundaryConditions::repeat_edge(in_);

        out_(x, y, c) = mux(c,
                            {D0 * in(2 * x - 1, y) + D1 * in(2 * x, y) + D2 * in(2 * x + 1, y) + D3 * in(2 * x + 2, y),
                             D3 * in(2 * x - 1, y) - D2 * in(2 * x, y) + D1 * in(2 * x + 1, y) - D0 * in(2 * x + 2, y)});
        out_.unroll(c, 2);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(daubechies_x, daubechies_x)
