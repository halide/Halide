#include "Halide.h"

#include "daubechies_constants.h"

namespace {

Halide::Var x("x"), y("y"), c("c");

class inverse_haar_x : public Halide::Generator<inverse_haar_x> {
public:
    ImageParam in_{ Float(32), 3, "in" };

    Func build() {
        Func in = Halide::BoundaryConditions::repeat_edge(in_);

        Func out("out");
        out(x, y) = select(x%2 == 0,
                           in(x/2, y, 0) + in(x/2, y, 1),
                           in(x/2, y, 0) - in(x/2, y, 1));
        out.unroll(x, 2);
        return out;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(inverse_haar_x, inverse_haar_x)
