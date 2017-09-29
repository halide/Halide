#include "Halide.h"

namespace {

class Multitarget : public Halide::Generator<Multitarget> {
public:
    Output<Buffer<uint32_t>> output{"output", 2};

    void generate() {
        Var x, y;
        if (get_target().has_feature(Target::Debug)) {
            output(x, y) = cast<uint32_t>((int32_t)0xdeadbeef);
        } else {
            output(x, y) = cast<uint32_t>((int32_t)0xf00dcafe);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Multitarget, multitarget)
