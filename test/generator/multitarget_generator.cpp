#include <stdint.h>
#include <memory>

#include "Halide.h"

namespace halide_register_generator {
struct halide_global_ns;
}  // namespace halide_register_generator

namespace {

class Multitarget : public Halide::Generator<Multitarget> {
public:
    Output<Buffer<uint32_t>> output{"output", 2};

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
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Multitarget, multitarget)
