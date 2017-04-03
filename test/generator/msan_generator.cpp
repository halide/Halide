#include "Halide.h"

namespace {

class MSAN : public Halide::Generator<MSAN> {
public:
    Output<Buffer<int32_t>> msan_output{"msan_output"};

    void generate() {
        // Currently the test just exercises Target::MSAN
        input(x, y, c) = cast<int32_t>(x + y + c);

        // This just makes an exact copy
        msan_extern_stage.define_extern("msan_extern_stage", {input}, Int(32), 3, NameMangling::C);

        RDom r(0, 4);
        msan_output(x, y, c) = sum(msan_extern_stage(r, y, c));

        // Add two update phases to be sure annotation happens post-update
        msan_output(r, y, c) += 1;
        msan_output(x, r, c) += 2;
    }

    void schedule() {
        input.compute_root();
        msan_extern_stage.compute_root();
        msan_output.parallel(y).vectorize(x, 4);
        msan_output.dim(0).set_stride(Expr()).set_extent(4)
                   .dim(1).set_extent(4)
                   .dim(2).set_extent(3);

    }
private:
    // Currently the test just exercises Target::MSAN
    Var x, y, c;

    Func input, msan_extern_stage;
};

Halide::RegisterGenerator<MSAN> register_my_gen{"msan"};

}  // namespace
