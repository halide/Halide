#include "Halide.h"

namespace {

class MSAN : public Halide::Generator<MSAN> {
public:
    Output<Func> msan_output{"msan_output", Int(32), 3};

    void generate() {
        // Currently the test just exercises Target::MSAN
        input(x, y, c) = cast<int32_t>(x + y + c);

        // This just makes an exact copy
        msan_extern_stage.define_extern("msan_extern_stage", {input}, Int(32), 3);

        RDom r(0, 4);
        msan_output(x, y, c) = sum(msan_extern_stage(r, y, c));

        // Add two update phases to be sure annotation happens post-update
        msan_output(r, y, c) += 1;
        msan_output(x, r, c) += 2;
    }

    void schedule() {
        input.compute_root();
        msan_extern_stage.compute_root();
        Func(msan_output).parallel(y).vectorize(x, 4);
        if (msan_output.has_buffer()) {
            msan_output.set_stride_constraint(0, Expr())
                       .set_extent_constraint(0, 4)
                       .set_extent_constraint(1, 4)
                       .set_extent_constraint(2, 3);
        }

    }
private:
    // Currently the test just exercises Target::MSAN
    Var x, y, c;

    Func input, msan_extern_stage;
};

Halide::RegisterGenerator<MSAN> register_my_gen{"msan"};

}  // namespace
