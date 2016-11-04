#include "Halide.h"

namespace {

class MSAN : public Halide::Generator<MSAN> {
public:
    Func build() {
        // Currently the test just exercises Target::MSAN
        Var x, y, c;

        Func input("input");
        input(x, y, c) = cast<int32_t>(x + y + c);

        // This just makes an exact copy
        Func msan_extern_stage;
        msan_extern_stage.define_extern("msan_extern_stage", {input}, Int(32), 3);

        RDom r(0, 4);
        Func msan_output("msan_output");
        msan_output(x, y, c) = sum(msan_extern_stage(r, y, c));

        // Add two update phases to be sure annotation happens post-update
        msan_output(r, y, c) += 1;
        msan_output(x, r, c) += 2;

        input.compute_root();
        msan_extern_stage.compute_root();
        msan_output.parallel(y).vectorize(x, 4);

        msan_output.output_buffer()
                   .set_stride(0, Expr())
                   .set_extent(0, 4)
                   .set_extent(1, 4)
                   .set_extent(2, 3);

        return msan_output;
    }
};

Halide::RegisterGenerator<MSAN> register_my_gen{"msan"};

}  // namespace
