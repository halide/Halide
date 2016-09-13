#include "Halide.h"

namespace {

class MSAN : public Halide::Generator<MSAN> {
public:
    Func build() {
        // Currently the test just exercises Target::MSAN
        Var x, y, c;
        Func f;
        f(x, y, c) = cast<int32_t>(x + y + c);
        f.output_buffer()
            .set_stride(0, Expr())
            .set_extent(0, 4)
            .set_extent(1, 4)
            .set_extent(2, 3);
        return f;
    }
};

Halide::RegisterGenerator<MSAN> register_my_gen{"msan"};

}  // namespace
