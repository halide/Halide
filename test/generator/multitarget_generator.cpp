#include "Halide.h"

namespace {

class Multitarget : public Halide::Generator<Multitarget> {
public:
    Func build() {
        Var x, y; 
        Func f("f");
        if (get_target().has_feature(Target::Debug)) {
            f(x, y) = cast<uint32_t>((int32_t)0xdeadbeef);
        } else {
            f(x, y) = cast<uint32_t>((int32_t)0xf00dcafe);
        }
        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Multitarget, multitarget)
