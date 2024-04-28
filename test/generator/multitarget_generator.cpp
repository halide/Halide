#include "Halide.h"

namespace {

class Multitarget : public Halide::Generator<Multitarget> {
public:
    Output<Buffer<uint32_t, 2>> output{"output"};
    Output<Buffer<float, 0>> random_float_output{"random_float_output"};
    Output<Buffer<int32_t, 0>> random_int_output{"random_int_output"};

    void generate() {
        Var x, y;
        // Note that 'NoBoundsQuery' is essentially a somewhat-arbitrary placeholder
        // here; we really just want to use a feature flag that doesn't require
        // a custom runtime (as does, e.g., Target::Debug).
        if (get_target().has_feature(Target::NoBoundsQuery)) {
            output(x, y) = cast<uint32_t>((int32_t)0xdeadbeef);
        } else {
            output(x, y) = cast<uint32_t>((int32_t)0xf00dcafe);
        }
        random_float_output() = Halide::random_float();
        random_int_output() = Halide::random_int();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Multitarget, multitarget)
