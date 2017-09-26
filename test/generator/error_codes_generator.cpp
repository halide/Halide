#include "Halide.h"

namespace {

class ErrorCodes : public Halide::Generator<ErrorCodes> {
public:
    Input<Buffer<int32_t>> input{ "input", 2};
    Input<int>             f_explicit_bound{"f_explicit_bound", 1, 0, 64};


    Func build() {
        target.set(get_target().without_feature(Target::LargeBuffers));
        Func f;
        Var x, y;

        f(x, y) = input(x, y);
        f.bound(x, 0, f_explicit_bound);

        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ErrorCodes, error_codes)

