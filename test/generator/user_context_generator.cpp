#include "Halide.h"

namespace {

class UserContext : public Halide::Generator<UserContext> {
public:
    Input<Buffer<float>> input{"input", 2};
    Output<Buffer<float>> output{"output", 2};

    void generate() {
        Var x, y;

        Func g;
        g(x, y) = input(x, y) * 2;
        g.compute_root();

        output(x, y) = g(x, y);

        output.parallel(y);
        trace_pipeline();

        // This test won't work in the profiler, because the profiler
        // insists on calling malloc with nullptr user context.
        assert(!get_target().has_feature(Target::Profile));
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(UserContext, user_context)
