#include "Halide.h"

namespace {

class ErrorCodes : public Halide::Generator<ErrorCodes> {
public:
    ImageParam input { Int(32), 2, "input" };
    Param<int> f_explicit_bound {"f_explicit_bound", 1, 0, 64};


    Func build() override {
        Func f;
        Var x, y;

        f(x, y) = input(x, y);
        f.bound(x, 0, f_explicit_bound);

        return f;
    }
};

Halide::RegisterGenerator<ErrorCodes> register_my_gen{"error_codes"};

}  // namespace
