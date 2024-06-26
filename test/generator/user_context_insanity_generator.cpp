#include "Halide.h"

namespace {

class UserContextInsanity : public Halide::Generator<UserContextInsanity> {
public:
    Input<Buffer<float, 2>> input{"input"};
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        Var x, y;

        Func g;
        g(x, y) = input(x, y) * 2;
        g.compute_root();

        output(x, y) = g(x, y);

        output.parallel(y);
        output.trace_stores();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(UserContextInsanity, user_context_insanity)
