#include "Halide.h"

namespace {

class MSAN : public Halide::Generator<MSAN> {
public:
    Func build() {
        // Currently the test just exercises Target::MSAN
        Var x, y, c;

        Func msan_output("msan_output");
        msan_output(x, y, c) = cast<int32_t>(x + y + c);

        // Add two update phases to be sure annotation happens post-update
        RDom r(0, 4);
        msan_output(r, y, c) += 1;
        msan_output(x, r, c) += 2;

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
