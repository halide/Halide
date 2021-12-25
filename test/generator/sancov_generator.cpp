#include "Halide.h"

namespace {

class SANCOV : public Halide::Generator<SANCOV> {
public:
    Output<Buffer<uint8_t>> output{"output", 3};

    void generate() {
        // Currently the test just exercises Target::SANCOV
        output(x, y, c) = cast<uint8_t>(42 + c);
    }

    void schedule() {
        output.dim(0).set_stride(Expr()).set_extent(4).dim(1).set_extent(4).dim(2).set_extent(3);
    }

private:
    // Currently the test just exercises Target::SANCOV
    Var x, y, c;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SANCOV, sancov)
