#include "Halide.h"

namespace {

class UserContext : public Halide::Generator<UserContext> {
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

        // This test won't work in the profiler, because the profiler
        // insists on calling malloc with nullptr user context.
        target.set(get_target().without_feature(Target::Profile));

        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(UserContext, user_context)
