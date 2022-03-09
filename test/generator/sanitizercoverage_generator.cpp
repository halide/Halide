#include "Halide.h"

namespace {

class SanitizerCoverage : public Halide::Generator<SanitizerCoverage> {
public:
    Output<Buffer<int8_t, 3>> output{"output"};

    void generate() {
        // Currently the test just exercises Target::SanitizerCoverage
        output(x, y, c) = cast<int8_t>(42 + c);
    }

    void schedule() {
        output.dim(0).set_stride(Expr()).set_extent(4).dim(1).set_extent(4).dim(2).set_extent(3);
    }

private:
    // Currently the test just exercises Target::SanitizerCoverage
    Var x, y, c;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SanitizerCoverage, sanitizercoverage)
