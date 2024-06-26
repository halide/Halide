#include "Halide.h"

namespace {

class ErrorCodes : public Halide::Generator<ErrorCodes> {
public:
    Input<Buffer<int32_t, 2>> input{"input"};
    Input<int> f_explicit_bound{"f_explicit_bound", 1, 0, 64};
    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        assert(!get_target().has_feature(Target::LargeBuffers));
        Var x, y;

        output(x, y) = input(x, y);
        output.bound(x, 0, f_explicit_bound);

        add_requirement(input.dim(1).extent() == 123);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ErrorCodes, error_codes)
