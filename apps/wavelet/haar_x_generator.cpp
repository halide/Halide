#include "Halide.h"

#include "daubechies_constants.h"

namespace {

Halide::Var x("x"), y("y"), c("c");

class haar_x : public Halide::Generator<haar_x> {
public:
    ImageParam in_{ Float(32), 2, "in" };

    Func build() {
        Func in = Halide::BoundaryConditions::repeat_edge(in_);

        Func out("out");
        out(x, y, c) = select(c == 0,
                              (in(2*x, y) + in(2*x+1, y)),
                              (in(2*x, y) - in(2*x+1, y)))/2;
        out.unroll(c, 2);
        return out;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(haar_x, haar_x)
