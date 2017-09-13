#include "Halide.h"

#include "daubechies_constants.h"

namespace {

Halide::Var x("x"), y("y"), c("c");

class inverse_daubechies_x : public Halide::Generator<inverse_daubechies_x> {
public:
    ImageParam in_{ Float(32), 3, "in" };

    Func build() {
        Func in = Halide::BoundaryConditions::repeat_edge(in_);

        Func out("out");
        out(x, y) = select(x%2 == 0,
                           D2*in(x/2, y, 0) + D1*in(x/2, y, 1) + D0*in(x/2+1, y, 0) + D3*in(x/2+1, y, 1),
                           D3*in(x/2, y, 0) - D0*in(x/2, y, 1) + D1*in(x/2+1, y, 0) - D2*in(x/2+1, y, 1));
        out.unroll(x, 2);
        return out;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(inverse_daubechies_x, inverse_daubechies_x)
