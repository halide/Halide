#include "Halide.h"

namespace {

class CanUseTarget : public Halide::Generator<CanUseTarget> {
public:
    Output<Buffer<uint32_t, 2>> output{"output"};

    // Current really just a placeholder: can_use_target_aottest.cpp just
    // needs to test the runtime itself, not the generator function.
    void generate() {
        Var x, y;
        output(x, y) = cast<uint32_t>((int32_t)0xdeadbeef);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(CanUseTarget, can_use_target)
