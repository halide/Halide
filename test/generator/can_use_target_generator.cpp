#include "Halide.h"

namespace {

class CanUseTarget : public Halide::Generator<CanUseTarget> {
public:
    // Current really just a placeholder: can_use_target_aottest.cpp just
    // needs to test the runtime itself, not the generator function.
    Func build() {
        Var x, y;
        Func f("f");
        f(x, y) = cast<uint32_t>((int32_t)0xdeadbeef);
        return f;
    }
};

Halide::RegisterGenerator<CanUseTarget> register_my_gen{"can_use_target"};

}  // namespace
