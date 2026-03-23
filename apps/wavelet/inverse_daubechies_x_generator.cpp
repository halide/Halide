#include "Halide.h"

#include "daubechies_constants.h"

namespace {

Halide::Var x("x"), y("y"), c("c");

class inverse_daubechies_x : public Halide::Generator<inverse_daubechies_x> {
public:
    Input<Buffer<float, 3>> in_{"in"};
    Output<Buffer<float, 2>> out_{"out"};

    void generate() {
        Func in = Halide::BoundaryConditions::repeat_edge(in_);

        out_(x, y) = select(x % 2 == 0,
                            D2 * in(x / 2, y, 0) + D1 * in(x / 2, y, 1) + D0 * in(x / 2 + 1, y, 0) + D3 * in(x / 2 + 1, y, 1),
                            D3 * in(x / 2, y, 0) - D0 * in(x / 2, y, 1) + D1 * in(x / 2 + 1, y, 0) - D2 * in(x / 2 + 1, y, 1));
        out_.unroll(x, 2);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(inverse_daubechies_x, inverse_daubechies_x)
