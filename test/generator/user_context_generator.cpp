#include "Halide.h"

namespace {

class UserContext : public Halide::Generator<UserContext> {
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
        trace_pipeline();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(UserContext, user_context)
