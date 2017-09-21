#include "Halide.h"

namespace {

class UserContextInsanity : public Halide::Generator<UserContextInsanity> {
public:
    Input<Buffer<float>> input{"input", 2};

    Func build() {
        Var x, y;

        Func g;
        g(x, y) = input(x, y) * 2;
        g.compute_root();

        Func f;
        f(x, y) = g(x, y);

        f.parallel(y);
        f.trace_stores();
        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(UserContextInsanity, user_context_insanity)
