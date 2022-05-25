#include "Halide.h"

namespace {

class MSAN : public Halide::Generator<MSAN> {
public:
    Input<Buffer<uint8_t, 3>> input{"input"};
    Output<Buffer<uint8_t, 3>> output{"output"};

    void generate() {
        // Currently the test just exercises Target::MSAN
        input_plus_1(x, y, c) = input(x, y, c) + 1;

        // This just makes an exact copy
        msan_extern_stage.define_extern("msan_extern_stage", {input_plus_1}, UInt(8), 3, NameMangling::C);

        RDom r(0, 4);
        output(x, y, c) = sum(msan_extern_stage(r, y, c));

        // Add two update phases to be sure annotation happens post-update
        output(r, y, c) += cast<uint8_t>(1);
        output(x, r, c) += cast<uint8_t>(2);
    }

    void schedule() {
        input_plus_1.compute_root();
        msan_extern_stage.compute_root();
        input.dim(0).set_stride(Expr()).set_extent(4).dim(1).set_extent(4).dim(2).set_extent(3);
        output.parallel(y).vectorize(x, 4);
        output.dim(0).set_stride(Expr()).set_extent(4).dim(1).set_extent(4).dim(2).set_extent(3);
        // Silence warnings.
        output.update(0).unscheduled();
        output.update(1).unscheduled();
    }

private:
    // Currently the test just exercises Target::MSAN
    Var x, y, c;

    Func input_plus_1, msan_extern_stage;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MSAN, msan)
